#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace punch2pen {

// One artist's learned vocabulary: the correction pairs produced by the
// plugin's click-to-correct loop. Lookups are case-sensitive on purpose so
// a stylised spelling survives. The free tier keeps one of these in memory
// for the engine session; each paid profile persists and syncs its own.
class Dictionary {
public:
  struct Entry {
    std::string original;
    std::string corrected;
    int count = 1;
    int64_t updatedAt = 0;    // ms since epoch, 0 when unknown
    bool pendingSync = false; // not yet acknowledged by the profile API
  };

  // Whisper's initial_prompt shares the text context with the decode, so a
  // few dozen ranked terms is the ceiling before inference degrades.
  static constexpr std::size_t kDefaultBiasTermCap = 64;

  void addCorrection(const std::string &original, const std::string &corrected,
                     int64_t nowMs, bool pendingSync);

  // Exact, case-sensitive replacement of one transcript token. Leading and
  // trailing punctuation is preserved. Unknown tokens come back untouched.
  std::string mapWord(const std::string &word) const;

  // Terms for TranscriberInterface::setVocabularyBias, ranked by correction
  // count then recency, capped so the prompt stays small.
  std::vector<std::string>
  vocabularyForBias(std::size_t cap = kDefaultBiasTermCap) const;

  std::size_t entryCount() const { return entries_.size(); }
  bool empty() const { return entries_.empty(); }
  std::vector<Entry> entries() const;
  std::vector<Entry> pendingEntries() const;
  void markSynced(const std::string &original);
  void clear();

  int64_t version() const { return version_; }
  void setVersion(int64_t version) { version_ = version; }

  // Adopt a server snapshot. Entries still pending locally win over the
  // snapshot so an offline correction is not lost by a refresh.
  void replaceFromCloud(const std::vector<Entry> &cloudEntries,
                        int64_t version);

  nlohmann::json toJson() const;
  static Dictionary fromJson(const nlohmann::json &json);

  bool saveToFile(const std::string &path) const;
  bool loadFromFile(const std::string &path);

  // Punctuation-stripped word split used for vocabulary extraction.
  static std::vector<std::string> extractTerms(const std::string &text);

private:
  std::map<std::string, Entry> entries_;
  int64_t version_ = 0;
};

} // namespace punch2pen
