#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h>

namespace punch2pen { class WebViewEditor; }

/**
    Plugin editor — thin shell that delegates to WebViewEditor.

    The bulk of the UI lives in Source/ui/public/index.html.
    This class is kept around because PluginProcessor::createEditor()
    constructs it by name and the host plugin binding requires the
    juce::AudioProcessorEditor subclass to live where it always has.
 */
class Punch2PenAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
  explicit Punch2PenAudioProcessorEditor(Punch2PenAudioProcessor &);
  ~Punch2PenAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  std::unique_ptr<punch2pen::WebViewEditor> webEditor;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Punch2PenAudioProcessorEditor)
};
