#pragma once

#include "CorrectionEditor.h"
#include "PluginProcessor.h"
#include "PositionDisplay.h"
#include <JuceHeader.h>

#include "TranscriptView.h"

class Punch2PenAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer {
public:
  Punch2PenAudioProcessorEditor(Punch2PenAudioProcessor &);
  ~Punch2PenAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  void timerCallback() override;

  Punch2PenAudioProcessor &audioProcessor;
  punch2pen::PositionDisplay positionDisplay;
  punch2pen::TranscriptView transcriptView;
  punch2pen::CorrectionEditor correctionEditor;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Punch2PenAudioProcessorEditor)
};
