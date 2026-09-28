#include "../Source/PluginProcessor.h"
#include "../Source/RingBuffer.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

class MockPlayHead : public juce::AudioPlayHead {
public:
  std::atomic<bool> recording{false};
  std::atomic<bool> playing{false};
  std::atomic<juce::int64> timeInSamples{0};

  juce::Optional<PositionInfo> getPosition() const override {
    PositionInfo info;
    info.setIsRecording(recording.load());
    info.setIsPlaying(playing.load());
    info.setBpm(120.0);
    info.setTimeInSamples(timeInSamples.load());
    info.setPpqPosition(0.0);
    return info;
  }
};

void fillBuffer(juce::AudioBuffer<float> &buffer, float value) {
  buffer.clear();
  for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
    for (int i = 0; i < buffer.getNumSamples(); ++i)
      buffer.setSample(ch, i, value);
  }
}

std::unique_ptr<Punch2PenAudioProcessor> makeProcessor(MockPlayHead &playHead) {
  auto proc = std::make_unique<Punch2PenAudioProcessor>();
  proc->enableAllBuses();
  proc->setPlayHead(&playHead);
  proc->setRateAndBufferSizeDetails(48000.0, 512);
  proc->prepareToPlay(48000.0, 512);
  return proc;
}

} // namespace

void testPlaybackDoesNotCapture() {
  MockPlayHead playHead;
  playHead.playing = true;
  playHead.recording = false;
  playHead.timeInSamples = 48000;

  auto proc = makeProcessor(playHead);
  auto *ring = proc->audioRingBufferForTest();
  assert(ring != nullptr);
  assert(ring->getNumReady() == 0);

  juce::AudioBuffer<float> buffer(2, 512);
  juce::MidiBuffer midi;
  fillBuffer(buffer, 0.25f);
  proc->processBlock(buffer, midi);

  assert(ring->getNumReady() == 0);
  assert(!proc->getTransportPosition().isRecording);

  proc->releaseResources();
  proc.reset();
  std::cout << "[PASS] testPlaybackDoesNotCapture" << std::endl;
}

void testRecordingCapturesFirstChannel() {
  MockPlayHead playHead;
  playHead.playing = true;
  playHead.recording = true;
  playHead.timeInSamples = 96000;

  auto proc = makeProcessor(playHead);
  auto *ring = proc->audioRingBufferForTest();
  assert(ring != nullptr);

  juce::AudioBuffer<float> buffer(2, 512);
  juce::MidiBuffer midi;
  fillBuffer(buffer, 0.5f);
  buffer.setSample(0, 0, 0.75f);
  proc->processBlock(buffer, midi);

  assert(ring->getNumReady() == 512);
  std::vector<float> out(512, 0.0f);
  double dawStart = -1.0;
  uint32_t epoch = 99;
  const int n = ring->read(out.data(), 512, &dawStart, &epoch);
  assert(n == 512);
  assert(dawStart == 96000.0);
  assert(epoch == 0);
  assert(out[0] == 0.75f);
  assert(out[1] == 0.5f);
  assert(proc->getTransportPosition().isRecording);

  proc->releaseResources();
  proc.reset();
  std::cout << "[PASS] testRecordingCapturesFirstChannel" << std::endl;
}

void testPunchOutIncrementsCaptureEpoch() {
  MockPlayHead playHead;
  playHead.playing = true;
  playHead.recording = true;
  playHead.timeInSamples = 0;

  auto proc = makeProcessor(playHead);
  juce::AudioBuffer<float> buffer(2, 64);
  juce::MidiBuffer midi;
  fillBuffer(buffer, 0.1f);
  proc->processBlock(buffer, midi);
  assert(proc->getCaptureEpoch() == 0);

  playHead.recording = false;
  playHead.timeInSamples = 64;
  proc->processBlock(buffer, midi);
  assert(proc->getCaptureEpoch() == 1);
  assert(!proc->getTransportPosition().isRecording);

  proc->releaseResources();
  proc.reset();
  std::cout << "[PASS] testPunchOutIncrementsCaptureEpoch" << std::endl;
}

int main() {
  juce::ScopedJuceInitialiser_GUI juceInit;

  testPlaybackDoesNotCapture();
  testRecordingCapturesFirstChannel();
  testPunchOutIncrementsCaptureEpoch();

  std::cout << "All PluginProcessor capture tests passed!" << std::endl;
  return 0;
}
