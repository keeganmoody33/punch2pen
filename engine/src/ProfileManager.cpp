#include "ProfileManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace punch2pen {

void ProfileManager::loadProfile(const std::string &userId) {
  currentUserId = userId;
  profileFilePath = dataDirectory + "/profile_" + userId + ".json";

  vocabulary.clear();
  corrections.clear();

  if (!std::filesystem::exists(profileFilePath)) {
    return;
  }

  std::ifstream inFile(profileFilePath);
  if (!inFile.is_open()) {
    std::cerr << "[ProfileManager] Failed to open profile: " << profileFilePath
              << std::endl;
    return;
  }

  try {
    json profileJson;
    inFile >> profileJson;

    if (profileJson.contains("vocabulary") && profileJson["vocabulary"].is_object()) {
      for (auto &[word, count] : profileJson["vocabulary"].items()) {
        vocabulary[word] = count.get<int>();
      }
    }

    if (profileJson.contains("corrections") && profileJson["corrections"].is_array()) {
      for (const auto &entry : profileJson["corrections"]) {
        CorrectionRecord rec;
        rec.original = entry.value("original", "");
        rec.corrected = entry.value("corrected", "");
        corrections.push_back(rec);
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[ProfileManager] JSON parse error: " << e.what() << std::endl;
  }
}

void ProfileManager::saveProfile() {
  if (profileFilePath.empty()) {
    return;
  }

  json profileJson;

  json vocabJson = json::object();
  for (const auto &[word, count] : vocabulary) {
    vocabJson[word] = count;
  }
  profileJson["vocabulary"] = vocabJson;

  json correctionsJson = json::array();
  for (const auto &c : corrections) {
    json entry;
    entry["original"] = c.original;
    entry["corrected"] = c.corrected;
    correctionsJson.push_back(entry);
  }
  profileJson["corrections"] = correctionsJson;

  profileJson["userId"] = currentUserId;

  std::ofstream outFile(profileFilePath);
  if (!outFile.is_open()) {
    std::cerr << "[ProfileManager] Failed to write profile: " << profileFilePath
              << std::endl;
    return;
  }

  outFile << profileJson.dump(2) << std::endl;
}

void ProfileManager::addVocabularyWord(const std::string &word) {
  vocabulary[word]++;
}

std::vector<std::string> ProfileManager::getVocabulary() const {
  std::vector<std::string> words;
  for (const auto &[word, count] : vocabulary) {
    if (count > 0) {
      words.push_back(word);
    }
  }
  return words;
}

void ProfileManager::addCorrection(const std::string &original,
                                   const std::string &corrected) {
  corrections.push_back({original, corrected});

  std::stringstream ss(corrected);
  std::string word;
  while (ss >> word) {
    word.erase(std::remove_if(word.begin(), word.end(), ::ispunct), word.end());
    if (!word.empty()) {
      addVocabularyWord(word);
    }
  }
}

} // namespace punch2pen
