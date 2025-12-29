#include "PluginEditor.h"
#include "PluginProcessor.h"

Punch2PenAudioProcessorEditor::Punch2PenAudioProcessorEditor(
    Punch2PenAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {

  setSize(400, 600);

  addAndMakeVisible(positionDisplay);
  addAndMakeVisible(transcriptView);

  // Connect display to processor state
  positionDisplay.setUpdateFunction([&p]() {
    auto transport = p.getTransportPosition();

    // Simple manual bar formula for calc (ignoring tempo changes for MVP)
    double beatsPerBar = (double)transport.timeSigNum;
    double totalBeats = transport.ppq; // PPQ is beats usually

    int bar = (int)(totalBeats / beatsPerBar) + 1;
    int beat = (int)fmod(totalBeats, beatsPerBar) + 1;

    return juce::String::formatted("Bar %d | Beat %d", bar, beat);
  });

  if (auto *client = p.getIPCClient()) {
    client->addListener(&transcriptView);
  }
}

Punch2PenAudioProcessorEditor::~Punch2PenAudioProcessorEditor() {
  if (auto *client = audioProcessor.getIPCClient()) {
    client->removeListener(&transcriptView);
  }
}

void Punch2PenAudioProcessorEditor::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xff1e1e1e)); // Dark theme

  // Header
  g.setColour(juce::Colour(0xff2d2d2d));
  g.fillRect(0, 0, getWidth(), 40);

  g.setColour(juce::Colours::white);
  g.setFont(14.0f);
  g.drawText("punch2pen", 10, 0, 100, 40, juce::Justification::centredLeft);
}

void Punch2PenAudioProcessorEditor::resized() {
  auto area = getLocalBounds();
  auto header = area.removeFromTop(40);

  // Position Display
  auto displayArea = area.removeFromTop(30);
  positionDisplay.setBounds(displayArea);

  transcriptView.setBounds(area);
}
