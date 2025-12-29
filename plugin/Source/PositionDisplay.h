#pragma once

#include <JuceHeader.h>

namespace punch2pen {

class PositionDisplay : public juce::Component, public juce::Timer {
public:
  PositionDisplay();
  ~PositionDisplay() override;

  void paint(juce::Graphics &g) override;
  void resized() override;

  void timerCallback() override;

  void setUpdateFunction(std::function<juce::String()> getPositionText);

private:
  juce::Label timeLabel;
  std::function<juce::String()> getPositionTextHelper;
};
} // namespace punch2pen
