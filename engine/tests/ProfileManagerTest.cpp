#include "../src/ProfileManager.h"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>

void testSaveAndLoad() {
  std::string tmpDir = "/tmp/punch2pen_test_profile";
  std::filesystem::create_directories(tmpDir);

  {
    punch2pen::ProfileManager pm;
    pm.setDataDirectory(tmpDir);
    pm.loadProfile("testuser");

    pm.addVocabularyWord("microphone");
    pm.addVocabularyWord("amplifier");
    pm.addCorrection("mic", "microphone");
    pm.saveProfile();
  }

  {
    punch2pen::ProfileManager pm;
    pm.setDataDirectory(tmpDir);
    pm.loadProfile("testuser");

    auto vocab = pm.getVocabulary();
    bool foundMic = std::find(vocab.begin(), vocab.end(), "microphone") != vocab.end();
    bool foundAmp = std::find(vocab.begin(), vocab.end(), "amplifier") != vocab.end();
    assert(foundMic && "Loaded profile should contain 'microphone'");
    assert(foundAmp && "Loaded profile should contain 'amplifier'");

    auto corrections = pm.getCorrections();
    assert(corrections.size() == 1 && "Should have 1 correction");
    assert(corrections[0].original == "mic" && "Original should be 'mic'");
    assert(corrections[0].corrected == "microphone" && "Corrected should be 'microphone'");
  }

  std::cout << "[PASS] testSaveAndLoad" << std::endl;

  std::filesystem::remove_all(tmpDir);
}

void testEmptyProfile() {
  std::string tmpDir = "/tmp/punch2pen_test_profile_empty";
  std::filesystem::create_directories(tmpDir);

  punch2pen::ProfileManager pm;
  pm.setDataDirectory(tmpDir);
  pm.loadProfile("nonexistent");

  auto vocab = pm.getVocabulary();
  assert(vocab.empty() && "New profile should have empty vocabulary");

  auto corrections = pm.getCorrections();
  assert(corrections.empty() && "New profile should have no corrections");

  assert(pm.getUserId() == "nonexistent" && "User ID should be set");

  std::cout << "[PASS] testEmptyProfile" << std::endl;

  std::filesystem::remove_all(tmpDir);
}

void testOverwriteProfile() {
  std::string tmpDir = "/tmp/punch2pen_test_profile_overwrite";
  std::filesystem::create_directories(tmpDir);

  {
    punch2pen::ProfileManager pm;
    pm.setDataDirectory(tmpDir);
    pm.loadProfile("user1");
    pm.addVocabularyWord("word1");
    pm.saveProfile();
  }

  {
    punch2pen::ProfileManager pm;
    pm.setDataDirectory(tmpDir);
    pm.loadProfile("user1");
    pm.addVocabularyWord("word2");
    pm.saveProfile();
  }

  {
    punch2pen::ProfileManager pm;
    pm.setDataDirectory(tmpDir);
    pm.loadProfile("user1");
    auto vocab = pm.getVocabulary();
    bool foundWord2 = std::find(vocab.begin(), vocab.end(), "word2") != vocab.end();
    assert(foundWord2 && "Overwritten profile should contain 'word2'");
  }

  std::cout << "[PASS] testOverwriteProfile" << std::endl;

  std::filesystem::remove_all(tmpDir);
}

int main() {
  testSaveAndLoad();
  testEmptyProfile();
  testOverwriteProfile();
  std::cout << "All ProfileManager tests passed!" << std::endl;
  return 0;
}
