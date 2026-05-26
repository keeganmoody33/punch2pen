#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "WebViewEditor.h"

// The native editor has been retired in favour of WebViewEditor.
// PluginProcessor::createEditor() still calls
// `new Punch2PenAudioProcessorEditor(*this)`, so we keep the same class
// name but route construction through the WebView bridge.
//
// We do this by making Punch2PenAudioProcessorEditor a thin shell that
// owns a WebViewEditor as a child component. Doing it this way means we
// don't have to touch PluginProcessor.cpp.

Punch2PenAudioProcessorEditor::Punch2PenAudioProcessorEditor(
    Punch2PenAudioProcessor &p)
    : AudioProcessorEditor(&p) {
  setSize(400, 600);
  webEditor = std::make_unique<punch2pen::WebViewEditor>(p);
  addAndMakeVisible(*webEditor);
}

Punch2PenAudioProcessorEditor::~Punch2PenAudioProcessorEditor() = default;

void Punch2PenAudioProcessorEditor::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xff1E1E1E));
}

void Punch2PenAudioProcessorEditor::resized() {
  if (webEditor)
    webEditor->setBounds(getLocalBounds());
}
