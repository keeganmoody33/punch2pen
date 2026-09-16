#include "../src/TranscriptTiming.h"

#include <cassert>
#include <cmath>
#include <iostream>

static bool approx(double a, double b) { return std::abs(a - b) < 1e-9; }

void testWhisperCentisecondsMapping() {
  // 1.50s into a buffer that started at DAW sample 48000, at 48 kHz → 48000 + 72000
  const double daw = punch2pen::whisperCentisecondsToDawSamples(48000.0, 150, 48000.0);
  assert(approx(daw, 48000.0 + 1.5 * 48000.0));

  const double same = punch2pen::whisperCentisecondsToDawSamples(96000.0, 0, 44100.0);
  assert(approx(same, 96000.0));

  const double noRate = punch2pen::whisperCentisecondsToDawSamples(100.0, 50, 0.0);
  assert(approx(noRate, 100.0));

  std::cout << "[PASS] testWhisperCentisecondsMapping" << std::endl;
}

void testSplitWordsProportional() {
  auto words = punch2pen::splitWordsAcrossRange("hi there", 0.0, 100.0);
  assert(words.size() == 2);
  assert(words[0].text == "hi");
  assert(words[1].text == "there");
  assert(approx(words[0].startSample, 0.0));
  assert(approx(words[0].endSample, 100.0 * (2.0 / 7.0)));
  assert(approx(words[1].startSample, words[0].endSample));
  assert(approx(words[1].endSample, 100.0));

  auto single = punch2pen::splitWordsAcrossRange("  hello  ", 48000.0, 52800.0);
  assert(single.size() == 1);
  assert(single[0].text == "hello");
  assert(approx(single[0].startSample, 48000.0));
  assert(approx(single[0].endSample, 52800.0));

  auto empty = punch2pen::splitWordsAcrossRange("   ", 0.0, 10.0);
  assert(empty.empty());

  std::cout << "[PASS] testSplitWordsProportional" << std::endl;
}

int main() {
  testWhisperCentisecondsMapping();
  testSplitWordsProportional();
  std::cout << "All TranscriptTiming tests passed!" << std::endl;
  return 0;
}
