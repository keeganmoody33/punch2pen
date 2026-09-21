#include "Dictionary.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>

namespace punch2pen {

namespace {

bool isTokenPunct(unsigned char c) {
  return std::ispunct(c) != 0 && c != '\'';
}

std::string trimTerm(const std::string &raw) {
  std::size_t begin = 0;
  std::size_t end = raw.size();
  while (begin < end && isTokenPunct(static_cast<unsigned char>(raw[begin])))
    ++begin;
  while (end > begin && isTokenPunct(static_cast<unsigned char>(raw[end - 1])))
    --end;
  return raw.substr(begin, end - begin);
}

} // namespace

std::vector<std::string> Dictionary::extractTerms(const std::string &text) {
  std::vector<std::string> terms;
  std::stringstream ss(text);
  std::string word;
  while (ss >> word) {
    const std::string term = trimTerm(word);
    if (!term.empty())
      terms.push_back(term);
  }
  return terms;
}

void Dictionary::addCorrection(const std::string &originalRaw,
                               const std::string &correctedRaw, int64_t nowMs,
                               bool pendingSync) {
  const std::string original = trimTerm(originalRaw);
  const std::string corrected = correctedRaw;
  if (original.empty() || corrected.empty() || original == corrected)
    return;

  auto it = entries_.find(original);
  if (it == entries_.end()) {
    Entry entry;
    entry.original = original;
    entry.corrected = corrected;
    entry.count = 1;
    entry.updatedAt = nowMs;
    entry.pendingSync = pendingSync;
    entries_.emplace(original, std::move(entry));
    return;
  }
  it->second.corrected = corrected;
  it->second.count += 1;
  it->second.updatedAt = nowMs;
  it->second.pendingSync = it->second.pendingSync || pendingSync;
}

std::string Dictionary::mapWord(const std::string &word) const {
  if (entries_.empty() || word.empty())
    return word;

  std::size_t begin = 0;
  std::size_t end = word.size();
  while (begin < end && isTokenPunct(static_cast<unsigned char>(word[begin])))
    ++begin;
  while (end > begin && isTokenPunct(static_cast<unsigned char>(word[end - 1])))
    --end;
  if (begin >= end)
    return word;

  const std::string core = word.substr(begin, end - begin);
  const auto it = entries_.find(core);
  if (it == entries_.end())
    return word;
  return word.substr(0, begin) + it->second.corrected + word.substr(end);
}

std::vector<std::string>
Dictionary::vocabularyForBias(std::size_t cap) const {
  struct Ranked {
    std::string term;
    int weight = 0;
    int64_t updatedAt = 0;
  };
  std::unordered_map<std::string, Ranked> byTerm;
  for (const auto &[original, entry] : entries_) {
    (void)original;
    for (const auto &term : extractTerms(entry.corrected)) {
      auto &ranked = byTerm[term];
      ranked.term = term;
      ranked.weight += entry.count;
      ranked.updatedAt = std::max(ranked.updatedAt, entry.updatedAt);
    }
  }

  std::vector<Ranked> ranked;
  ranked.reserve(byTerm.size());
  for (auto &[term, r] : byTerm) {
    (void)term;
    ranked.push_back(std::move(r));
  }
  std::sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) {
    if (a.weight != b.weight)
      return a.weight > b.weight;
    if (a.updatedAt != b.updatedAt)
      return a.updatedAt > b.updatedAt;
    return a.term < b.term;
  });

  std::vector<std::string> out;
  for (const auto &r : ranked) {
    if (out.size() >= cap)
      break;
    out.push_back(r.term);
  }
  return out;
}

std::vector<Dictionary::Entry> Dictionary::entries() const {
  std::vector<Entry> out;
  out.reserve(entries_.size());
  for (const auto &[original, entry] : entries_) {
    (void)original;
    out.push_back(entry);
  }
  return out;
}

std::vector<Dictionary::Entry> Dictionary::pendingEntries() const {
  std::vector<Entry> out;
  for (const auto &[original, entry] : entries_) {
    (void)original;
    if (entry.pendingSync)
      out.push_back(entry);
  }
  return out;
}

void Dictionary::markSynced(const std::string &original) {
  auto it = entries_.find(original);
  if (it != entries_.end())
    it->second.pendingSync = false;
}

void Dictionary::clear() {
  entries_.clear();
  version_ = 0;
}

void Dictionary::replaceFromCloud(const std::vector<Entry> &cloudEntries,
                                  int64_t version) {
  std::map<std::string, Entry> merged;
  for (const auto &entry : cloudEntries) {
    if (entry.original.empty() || entry.corrected.empty())
      continue;
    Entry copy = entry;
    copy.pendingSync = false;
    merged[copy.original] = std::move(copy);
  }
  for (const auto &[original, entry] : entries_) {
    if (entry.pendingSync)
      merged[original] = entry;
  }
  entries_ = std::move(merged);
  version_ = version;
}

nlohmann::json Dictionary::toJson() const {
  nlohmann::json entries = nlohmann::json::array();
  for (const auto &[original, entry] : entries_) {
    (void)original;
    nlohmann::json item;
    item["original"] = entry.original;
    item["corrected"] = entry.corrected;
    item["count"] = entry.count;
    item["updatedAt"] = entry.updatedAt;
    if (entry.pendingSync)
      item["pendingSync"] = true;
    entries.push_back(std::move(item));
  }
  nlohmann::json out;
  out["version"] = version_;
  out["entries"] = std::move(entries);
  return out;
}

Dictionary Dictionary::fromJson(const nlohmann::json &json) {
  Dictionary dict;
  if (!json.is_object())
    return dict;
  dict.version_ = json.value("version", int64_t{0});
  const auto it = json.find("entries");
  if (it == json.end() || !it->is_array())
    return dict;
  for (const auto &item : *it) {
    if (!item.is_object())
      continue;
    Entry entry;
    entry.original = item.value("original", std::string{});
    entry.corrected = item.value("corrected", std::string{});
    entry.count = std::max(1, item.value("count", 1));
    entry.updatedAt = item.value("updatedAt", int64_t{0});
    entry.pendingSync = item.value("pendingSync", false);
    if (entry.original.empty() || entry.corrected.empty())
      continue;
    dict.entries_[entry.original] = std::move(entry);
  }
  return dict;
}

bool Dictionary::saveToFile(const std::string &path) const {
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path(), ec);
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out.is_open()) {
      std::cerr << "[Dictionary] cannot write " << tmp << std::endl;
      return false;
    }
    out << toJson().dump(2) << std::endl;
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::cerr << "[Dictionary] cannot move " << tmp << " to " << path << ": "
              << ec.message() << std::endl;
    return false;
  }
  return true;
}

bool Dictionary::loadFromFile(const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open())
    return false;
  try {
    nlohmann::json json;
    in >> json;
    *this = fromJson(json);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[Dictionary] parse error in " << path << ": " << e.what()
              << std::endl;
    return false;
  }
}

} // namespace punch2pen
