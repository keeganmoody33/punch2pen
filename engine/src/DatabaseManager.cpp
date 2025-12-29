#include "DatabaseManager.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace punch2pen {

DatabaseManager::DatabaseManager() {}
DatabaseManager::~DatabaseManager() {}

bool DatabaseManager::initialize(const std::string &dbPath) {
  databasePath = dbPath;
  loadFromFile();
  return true;
}

void DatabaseManager::addCorrection(const std::string &original,
                                    const std::string &corrected) {
  std::lock_guard<std::mutex> lock(dataLock);

  // Simple storage logic
  corrections.push_back({original, corrected, 1});

  // Extract words from corrected text as vocabulary
  std::stringstream ss(corrected);
  std::string word;
  while (ss >> word) {
    // strip punctuation
    word.erase(std::remove_if(word.begin(), word.end(), ::ispunct), word.end());
    if (!word.empty()) {
      vocabulary[word]++;
    }
  }

  saveToFile();
}

std::vector<std::string> DatabaseManager::getVocabulary() {
  std::lock_guard<std::mutex> lock(dataLock);
  std::vector<std::string> words;
  for (const auto &pair : vocabulary) {
    if (pair.second > 0) { // Naive filtering
      words.push_back(pair.first);
    }
  }
  return words;
}

void DatabaseManager::loadFromFile() {
  std::lock_guard<std::mutex> lock(dataLock);
  std::ifstream inFile(databasePath);
  if (!inFile.is_open())
    return;

  // Very basic CSV parsing: original,corrected
  std::string line;
  while (std::getline(inFile, line)) {
    size_t comma = line.find(',');
    if (comma != std::string::npos) {
      std::string orig = line.substr(0, comma);
      std::string corr = line.substr(comma + 1);
      corrections.push_back({orig, corr, 1});

      // Populate vocab (duplicate logic for now)
      std::stringstream ss(corr);
      std::string word;
      while (ss >> word) {
        word.erase(std::remove_if(word.begin(), word.end(), ::ispunct),
                   word.end());
        if (!word.empty())
          vocabulary[word]++;
      }
    }
  }
}

void DatabaseManager::saveToFile() {
  // Assume lock held by caller
  std::ofstream outFile(databasePath);
  if (!outFile.is_open())
    return;

  for (const auto &c : corrections) {
    outFile << c.original << "," << c.corrected << "\n";
  }
}

} // namespace punch2pen
