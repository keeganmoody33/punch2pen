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

void testCloudDeltaAssemblerWaitsForCompletion() {
  punch2pen::CloudDeltaAssembler stream;
  stream.captureLiveOrigin(48000.0);

  // Queued host audio must not move sentEnd; only sent PCM does.
  assert(stream.live.sentEnd == 48000.0);
  stream.noteLivePcmSent(16000, 48000.0, 16000); // 1s of 16 kHz → 48000 host
  assert(std::abs(stream.live.sentEnd - 96000.0) < 1e-9);

  assert(!stream.addDelta(""));
  assert(!stream.addDelta("   "));
  assert(stream.pending.empty());
  assert(stream.live.sentEnd == 96000.0);

  assert(stream.addDelta("hel"));
  assert(stream.addDelta("lo world"));
  // Partial deltas are buffered, not split into fake words.
  assert(stream.pending == "hello world");

  stream.finalize();
  assert(!stream.live.active);
  assert(stream.committed.active);
  assert(std::abs(stream.committed.origin - 48000.0) < 1e-9);
  assert(std::abs(stream.committed.sentEnd - 96000.0) < 1e-9);

  auto words = stream.complete();
  assert(words.size() == 2);
  assert(words[0].text == "hello");
  assert(words[1].text == "world");
  assert(approx(words[0].startSample, 48000.0));
  assert(approx(words[1].endSample, 96000.0));
  assert(!stream.committed.active);
  assert(stream.pending.empty());

  std::cout << "[PASS] testCloudDeltaAssemblerWaitsForCompletion" << std::endl;
}

void testCloudDeltaAssemblerEmptyDeltaDoesNotConsumeWindow() {
  punch2pen::CloudDeltaAssembler stream;
  stream.captureLiveOrigin(0.0);
  stream.noteLivePcmSent(8000, 48000.0, 16000); // 0.5s → 24000 host samples
  stream.finalize();

  assert(!stream.addDelta(""));
  auto none = stream.complete("");
  assert(none.empty());

  // A later complete after a real delta still has a window if we didn't
  // consume it on empty... but complete() always releases committed.
  // Re-seed to show empty-then-real on the live window before finalize:
  punch2pen::CloudDeltaAssembler liveStream;
  liveStream.captureLiveOrigin(1000.0);
  liveStream.noteLivePcmSent(16000, 48000.0, 16000);
  assert(!liveStream.addDelta(""));
  assert(liveStream.addDelta("ok"));
  auto words = liveStream.complete();
  assert(words.size() == 1);
  assert(words[0].text == "ok");
  assert(approx(words[0].startSample, 1000.0));
  assert(approx(words[0].endSample, 49000.0));

  std::cout << "[PASS] testCloudDeltaAssemblerEmptyDeltaDoesNotConsumeWindow"
            << std::endl;
}

int main() {
  testWhisperCentisecondsMapping();
  testSplitWordsProportional();
  testCloudDeltaAssemblerWaitsForCompletion();
  testCloudDeltaAssemblerEmptyDeltaDoesNotConsumeWindow();
  std::cout << "All TranscriptTiming tests passed!" << std::endl;
  return 0;
}
