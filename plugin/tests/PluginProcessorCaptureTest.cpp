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

void testStoppedTransportDoesNotCapture() {
  MockPlayHead playHead;
  playHead.playing = false;
  playHead.recording = false;
  playHead.timeInSamples = 48000;

  auto proc = makeProcessor(playHead);
  auto *ring = proc->audioRingBufferForTest();
  assert(ring != nullptr);

  juce::AudioBuffer<float> buffer(2, 512);
  juce::MidiBuffer midi;
  fillBuffer(buffer, 0.25f);
  proc->processBlock(buffer, midi);

  assert(ring->getNumReady() == 0);
  assert(proc->getCaptureEpoch() == 0);
  assert(!proc->getTransportPosition().isPlaying);
  assert(!proc->getTransportPosition().isRecording);
  assert(buffer.getSample(0, 0) == 0.25f);

  proc->releaseResources();
  proc.reset();
  std::cout << "[PASS] testStoppedTransportDoesNotCapture" << std::endl;
}

void testNonRecordingPlaybackCapturesOnHostClock() {
  MockPlayHead playHead;
  playHead.playing = true;
  playHead.recording = false;
  playHead.timeInSamples = 48000;

  auto proc = makeProcessor(playHead);
  assert(proc->getLatencySamples() == 0);
  assert(proc->getTailLengthSeconds() == 0.0);
  auto *ring = proc->audioRingBufferForTest();
  assert(ring != nullptr);
  assert(ring->getNumReady() == 0);

  juce::AudioBuffer<float> buffer(2, 512);
  juce::MidiBuffer midi;
  fillBuffer(buffer, 0.25f);
  buffer.setSample(0, 0, 0.8f);
  buffer.setSample(1, 0, -0.4f);
  proc->processBlock(buffer, midi);

  // Passthrough: transcription is a side path. Channel 0 is the vocal.
  assert(buffer.getSample(0, 0) == 0.8f);
  assert(buffer.getSample(0, 1) == 0.25f);
  assert(buffer.getSample(1, 0) == -0.4f);

  assert(ring->getNumReady() == 512);
  std::vector<float> out(512, 0.0f);
  double dawStart = -1.0;
  uint32_t epoch = 99;
  const int n = ring->read(out.data(), 512, &dawStart, &epoch);
  assert(n == 512);
  assert(dawStart == 48000.0);
  assert(epoch == 0);
  assert(out[0] == 0.8f);
  assert(out[1] == 0.25f);
  assert(proc->getCaptureEpoch() == 0);
  assert(proc->getTransportPosition().isPlaying);
  assert(!proc->getTransportPosition().isRecording);
  assert(proc->getHostDawSampleTime() == 48000.0);

  playHead.timeInSamples = 48512;
  fillBuffer(buffer, 0.1f);
  proc->processBlock(buffer, midi);
  assert(ring->getNumReady() == 512);
  dawStart = -1.0;
  epoch = 99;
  const int n2 = ring->read(out.data(), 512, &dawStart, &epoch);
  assert(n2 == 512);
  assert(dawStart == 48512.0);
  assert(epoch == 0);
  assert(out[0] == 0.1f);

  playHead.playing = false;
  playHead.timeInSamples = 49024;
  proc->processBlock(buffer, midi);
  assert(proc->getCaptureEpoch() == 1);
  assert(ring->getNumReady() == 0);
  assert(!proc->getTransportPosition().isPlaying);

  proc->releaseResources();
  proc.reset();
  std::cout << "[PASS] testNonRecordingPlaybackCapturesOnHostClock" << std::endl;
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
  assert(proc->getTransportPosition().isPlaying);
  assert(!proc->getTransportPosition().isRecording);

  // The recording take stays on epoch 0. Playback that continues after
  // punch-out is a new pass, still on the host clock.
  auto *ring = proc->audioRingBufferForTest();
  std::vector<float> out(64, 0.0f);
  double dawStart = -1.0;
  uint32_t epoch = 99;
  assert(ring->read(out.data(), 64, &dawStart, &epoch) == 64);
  assert(dawStart == 0.0);
  assert(epoch == 0);
  dawStart = -1.0;
  epoch = 99;
  assert(ring->read(out.data(), 64, &dawStart, &epoch) == 64);
  assert(dawStart == 64.0);
  assert(epoch == 1);

  proc->releaseResources();
  proc.reset();
  std::cout << "[PASS] testPunchOutIncrementsCaptureEpoch" << std::endl;
}

int main() {
  juce::ScopedJuceInitialiser_GUI juceInit;

  testStoppedTransportDoesNotCapture();
  testNonRecordingPlaybackCapturesOnHostClock();
  testRecordingCapturesFirstChannel();
  testPunchOutIncrementsCaptureEpoch();

  std::cout << "All PluginProcessor capture tests passed!" << std::endl;
  return 0;
}
