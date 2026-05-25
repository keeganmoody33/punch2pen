#include "PluginEditor.h"
#include "PluginProcessor.h"

Punch2PenAudioProcessorEditor::Punch2PenAudioProcessorEditor(
    Punch2PenAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {

  setSize(400, 600);

  addAndMakeVisible(positionDisplay);
  addAndMakeVisible(transcriptView);
  addChildComponent(correctionEditor);

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
    correctionEditor.setIPCClient(client);
  }

  transcriptView.setWordClickCallback(
      [this](const std::string &word, juce::Point<int> screenPos) {
        auto localPos = getLocalPoint(nullptr, screenPos);
        int x = juce::jlimit(0, getWidth() - 260, localPos.x);
        int y = juce::jlimit(0, getHeight() - 90, localPos.y);
        correctionEditor.show(word, {x, y});
      });

  startTimerHz(30);
}

Punch2PenAudioProcessorEditor::~Punch2PenAudioProcessorEditor() {
  stopTimer();
  if (auto *client = audioProcessor.getIPCClient()) {
    client->removeListener(&transcriptView);
  }
}

void Punch2PenAudioProcessorEditor::timerCallback() {
  auto transport = audioProcessor.getTransportPosition();
  double sampleRate = audioProcessor.getSampleRate();
  if (sampleRate <= 0.0)
    sampleRate = 48000.0;
  double currentSamplePosition = transport.ppq * (60.0 / transport.bpm) * sampleRate;
  transcriptView.updatePlaybackPosition(currentSamplePosition);
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
  area.removeFromTop(40);

  // Position Display
  auto displayArea = area.removeFromTop(30);
  positionDisplay.setBounds(displayArea);

  transcriptView.setBounds(area);
}
