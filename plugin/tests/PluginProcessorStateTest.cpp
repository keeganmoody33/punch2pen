#include "../Source/PluginProcessor.h"

#include <cassert>
#include <iostream>

void testStateRoundTrip() {
  juce::ScopedJuceInitialiser_GUI init;

  // Create a processor and set transcription mode to Online
  auto proc1 = std::make_unique<Punch2PenAudioProcessor>();
  proc1->getIPCClient()->setTranscriptionMode(
      punch2pen::IPCClient::TranscriptionMode::Online);

  // Serialize state
  juce::MemoryBlock destData;
  proc1->getStateInformation(destData);
  proc1.reset();

  // Create a fresh processor and restore state
  auto proc2 = std::make_unique<Punch2PenAudioProcessor>();
  assert(proc2->getIPCClient()->getTranscriptionMode() ==
         punch2pen::IPCClient::TranscriptionMode::Offline);

  proc2->setStateInformation(destData.getData(), (int)destData.getSize());
  assert(proc2->getIPCClient()->getTranscriptionMode() ==
         punch2pen::IPCClient::TranscriptionMode::Online);

  proc2.reset();

  std::cout << "[PASS] testStateRoundTrip" << std::endl;
}

int main() {
  testStateRoundTrip();
  std::cout << "All PluginProcessorState tests passed!" << std::endl;
  return 0;
}
