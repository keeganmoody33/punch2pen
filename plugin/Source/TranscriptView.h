#pragma once

#include "IPCClient.h"
#include <JuceHeader.h>
#include <functional>
#include <string>
#include <vector>

namespace punch2pen {

struct LyricWord {
  std::string text;
  double startSample = 0.0;
  double endSample = 0.0;
  float currentAlpha = 0.4f;
};

struct LyricLine {
  std::vector<LyricWord> words;
  double lineStartSample = 0.0;
  int barNumber = 1;
};

class TranscriptView : public juce::Component, public IPCClient::Listener {
public:
  TranscriptView();
  ~TranscriptView() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &event) override;

  void appendStreamingText(const std::string &newText, double sampleTime,
                           bool isProvisional);
  void updatePlaybackPosition(double currentDAWSample);

  // IPCClient::Listener implementation
  void onTranscriptionReceived(const std::string &text) override;
  void onStatusChanged(bool isConnected) override;

  using WordClickCallback = std::function<void(const std::string &word,
                                               juce::Point<int> position)>;
  void setWordClickCallback(WordClickCallback callback);

private:
  struct WordHitBox {
    juce::Rectangle<int> bounds;
    std::string text;
  };

  std::vector<LyricLine> transcriptLines;
  std::vector<WordHitBox> wordHitBoxes;
  double lastKnownPlayheadPosition = 0.0;
  double streamCursorSample = 0.0;

  float currentScrollY = 0.0f;
  float targetScrollY = 0.0f;

  std::unique_ptr<juce::VBlankAttachment> vBlankAttachment;
  juce::CriticalSection dataLock;
  bool isConnected = false;

  WordClickCallback onWordClicked;

  const juce::Colour bgPrimary = juce::Colour::fromRGB(28, 25, 23);
  const juce::Colour textPrimary = juce::Colour::fromRGB(250, 250, 249);
  const juce::Colour accentPrimary = juce::Colour::fromRGB(252, 211, 77);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranscriptView)
};

} // namespace punch2pen
