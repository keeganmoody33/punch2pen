#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace punch2pen {

class ProfileManager {
public:
  ProfileManager() = default;
  ~ProfileManager() = default;

  void loadProfile(const std::string &userId);
  void saveProfile();

  const std::string &getUserId() const { return currentUserId; }
  const std::string &getProfilePath() const { return profileFilePath; }

  void setDataDirectory(const std::string &dir) { dataDirectory = dir; }

  void addVocabularyWord(const std::string &word);
  std::vector<std::string> getVocabulary() const;

  void addCorrection(const std::string &original, const std::string &corrected);

  struct CorrectionRecord {
    std::string original;
    std::string corrected;
  };
  const std::vector<CorrectionRecord> &getCorrections() const {
    return corrections;
  }

private:
  std::string currentUserId;
  std::string dataDirectory;
  std::string profileFilePath;

  std::unordered_map<std::string, int> vocabulary;
  std::vector<CorrectionRecord> corrections;
};

} // namespace punch2pen
