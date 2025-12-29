#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace punch2pen {

class DatabaseManager {
public:
  DatabaseManager();
  ~DatabaseManager();

  bool initialize(const std::string &dbPath);

  // Add a correction pair (user spoke -> corrected text)
  void addCorrection(const std::string &original, const std::string &corrected);

  // Retrieve custom vocabulary (words extracted from corrections)
  std::vector<std::string> getVocabulary();

private:
  void loadFromFile();
  void saveToFile();

  std::string databasePath;
  std::mutex dataLock;

  // For MVP, we store in memory and sync to text file
  struct Correction {
    std::string original;
    std::string corrected;
    int count = 1;
  };

  std::vector<Correction> corrections;
  std::unordered_map<std::string, int> vocabulary;
};

} // namespace punch2pen
