#pragma once

#include "IPCClient.h"
#include <JuceHeader.h>

namespace punch2pen {
class AudioRingBuffer;
}

class Punch2PenAudioProcessor : public juce::AudioProcessor {
public:
  Punch2PenAudioProcessor();
  ~Punch2PenAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
#endif

  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int programIndex) override;
  const juce::String getProgramName(int programIndex) override;
  void changeProgramName(int programIndex,
                         const juce::String &newName) override;

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  // Public for Editor access
  struct TransportPosition {
    int bar = 1;
    int beat = 1;
    double ppq = 0.0;
    double bpm = 120.0;
    int timeSigNum = 4; // Assuming 4/4 default
    int timeSigDenom = 4;
    bool isPlaying = false;
    bool isRecording = false;
  };

  TransportPosition getTransportPosition() const;

  punch2pen::IPCClient *getIPCClient() const { return ipcClient.get(); }

private:
  std::unique_ptr<punch2pen::AudioRingBuffer> audioRingBuffer;
  std::unique_ptr<punch2pen::IPCClient> ipcClient;

  std::atomic<double> currentBpm{120.0};

  // Avoid heap alloc in processBlock
  TransportPosition lastTransportPosition;
  bool wasRecordingLastBlock = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Punch2PenAudioProcessor)
};
