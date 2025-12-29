#pragma once

#include "IPCClient.h"
#include <JuceHeader.h>

namespace punch2pen {

class TranscriptView : public juce::Component, public IPCClient::Listener {
public:
  TranscriptView();
  ~TranscriptView() override;

  void paint(juce::Graphics &g) override;
  void resized() override;

  // IPCClient::Listener implementation
  void onTranscriptionReceived(const std::string &text) override;
  void onStatusChanged(bool isConnected) override;

private:
  juce::TextEditor textEditor;
  bool isConnected = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranscriptView);
};

} // namespace punch2pen
