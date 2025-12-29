#pragma once

#include "PluginProcessor.h"
#include "PositionDisplay.h"
#include <JuceHeader.h>

#include "TranscriptView.h"

class Punch2PenAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
  Punch2PenAudioProcessorEditor(Punch2PenAudioProcessor &);
  ~Punch2PenAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  Punch2PenAudioProcessor &audioProcessor;
  punch2pen::PositionDisplay positionDisplay;
  punch2pen::TranscriptView transcriptView;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Punch2PenAudioProcessorEditor)
};
