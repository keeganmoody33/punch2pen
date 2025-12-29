#include "PositionDisplay.h"

namespace punch2pen {

PositionDisplay::PositionDisplay() {
  addAndMakeVisible(timeLabel);
  timeLabel.setJustificationType(juce::Justification::centred);
  timeLabel.setFont(juce::Font(24.0f, juce::Font::bold));

  startTimerHz(30); // Update UI at 30Hz
}

PositionDisplay::~PositionDisplay() { stopTimer(); }

void PositionDisplay::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black.withAlpha(0.2f));
  g.setColour(juce::Colours::white.withAlpha(0.1f));
  g.drawRect(getLocalBounds());
}

void PositionDisplay::resized() { timeLabel.setBounds(getLocalBounds()); }

void PositionDisplay::timerCallback() {
  if (getPositionTextHelper) {
    timeLabel.setText(getPositionTextHelper(), juce::dontSendNotification);
  }
}

void PositionDisplay::setUpdateFunction(
    std::function<juce::String()> getPositionText) {
  getPositionTextHelper = getPositionText;
}

} // namespace punch2pen
