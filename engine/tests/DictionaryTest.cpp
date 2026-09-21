#include "../src/Dictionary.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

void testMapWordIsCaseSensitiveAndKeepsPunctuation() {
  punch2pen::Dictionary dict;
  dict.addCorrection("nah", "hell nah", 1000, false);
  dict.addCorrection("Punch", "Punch2Pen", 1001, false);

  assert(dict.mapWord("nah") == "hell nah");
  assert(dict.mapWord("nah,") == "hell nah,");
  assert(dict.mapWord("\"nah\"") == "\"hell nah\"");
  assert(dict.mapWord("Nah") == "Nah" && "map must be case-sensitive");
  assert(dict.mapWord("Punch") == "Punch2Pen");
  assert(dict.mapWord("punch") == "punch");
  assert(dict.mapWord("unrelated") == "unrelated");
  assert(dict.mapWord("") == "");
  assert(dict.mapWord("...") == "...");

  std::cout << "[PASS] testMapWordIsCaseSensitiveAndKeepsPunctuation"
            << std::endl;
}

void testVocabularyRankedAndCapped() {
  punch2pen::Dictionary dict;
  dict.addCorrection("a", "alpha", 10, false);
  dict.addCorrection("a", "alpha", 11, false);
  dict.addCorrection("a", "alpha", 12, false);
  dict.addCorrection("b", "bravo tango", 20, false);
  dict.addCorrection("c", "charlie", 30, false);

  const auto vocab = dict.vocabularyForBias();
  assert(vocab.size() == 4);
  assert(vocab[0] == "alpha" && "most-corrected term ranks first");
  assert(contains(vocab, "bravo") && contains(vocab, "tango") &&
         contains(vocab, "charlie"));

  const auto capped = dict.vocabularyForBias(2);
  assert(capped.size() == 2);
  assert(capped[0] == "alpha");

  punch2pen::Dictionary big;
  for (int i = 0; i < 500; ++i)
    big.addCorrection("w" + std::to_string(i), "term" + std::to_string(i), i,
                      false);
  assert(big.vocabularyForBias().size() ==
             punch2pen::Dictionary::kDefaultBiasTermCap &&
         "default cap keeps the whisper prompt small");

  std::cout << "[PASS] testVocabularyRankedAndCapped" << std::endl;
}

void testIgnoresEmptyAndNoOpCorrections() {
  punch2pen::Dictionary dict;
  dict.addCorrection("", "x", 1, false);
  dict.addCorrection("x", "", 1, false);
  dict.addCorrection("same", "same", 1, false);
  assert(dict.empty());
  std::cout << "[PASS] testIgnoresEmptyAndNoOpCorrections" << std::endl;
}

void testJsonRoundTripAndFile() {
  const std::string dir = "/tmp/punch2pen_test_dictionary";
  std::filesystem::remove_all(dir);
  const std::string path = dir + "/nested/profile.json";

  punch2pen::Dictionary dict;
  dict.addCorrection("helo", "hello", 5, true);
  dict.addCorrection("mic", "microphone", 6, false);
  dict.setVersion(42);
  assert(dict.saveToFile(path) && "saveToFile creates parent directories");

  punch2pen::Dictionary loaded;
  assert(loaded.loadFromFile(path));
  assert(loaded.entryCount() == 2);
  assert(loaded.version() == 42);
  assert(loaded.mapWord("helo") == "hello");
  assert(loaded.pendingEntries().size() == 1);
  assert(loaded.pendingEntries()[0].original == "helo");

  punch2pen::Dictionary missing;
  assert(!missing.loadFromFile(dir + "/nope.json"));
  assert(missing.empty());

  std::filesystem::remove_all(dir);
  std::cout << "[PASS] testJsonRoundTripAndFile" << std::endl;
}

void testReplaceFromCloudKeepsPendingLocalEntries() {
  punch2pen::Dictionary dict;
  dict.addCorrection("offline", "made offline", 1, true);
  dict.addCorrection("stale", "old value", 1, false);

  std::vector<punch2pen::Dictionary::Entry> cloud;
  cloud.push_back({"stale", "server value", 3, 99, false});
  cloud.push_back({"remote", "from another room", 1, 98, false});
  dict.replaceFromCloud(cloud, 7);

  assert(dict.version() == 7);
  assert(dict.entryCount() == 3);
  assert(dict.mapWord("stale") == "server value");
  assert(dict.mapWord("remote") == "from another room");
  assert(dict.mapWord("offline") == "made offline" &&
         "pending local correction survives a refresh");
  assert(dict.pendingEntries().size() == 1);

  dict.markSynced("offline");
  assert(dict.pendingEntries().empty());

  dict.clear();
  assert(dict.empty() && dict.version() == 0);

  std::cout << "[PASS] testReplaceFromCloudKeepsPendingLocalEntries"
            << std::endl;
}

} // namespace

int main() {
  testMapWordIsCaseSensitiveAndKeepsPunctuation();
  testVocabularyRankedAndCapped();
  testIgnoresEmptyAndNoOpCorrections();
  testJsonRoundTripAndFile();
  testReplaceFromCloudKeepsPendingLocalEntries();
  std::cout << "All Dictionary tests passed!" << std::endl;
  return 0;
}
