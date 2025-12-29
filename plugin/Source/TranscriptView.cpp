#include "TranscriptView.h"

namespace punch2pen {

TranscriptView::TranscriptView() {
  addAndMakeVisible(textEditor);
  textEditor.setMultiLine(true);
  textEditor.setReadOnly(true);
  textEditor.setScrollbarsShown(true);
  textEditor.setCaretVisible(false);
  textEditor.setFont(juce::FontOptions(16.0f));
  textEditor.setText("Waiting for transcription service...");

  // Style
  textEditor.setColour(juce::TextEditor::backgroundColourId,
                       juce::Colour(0xff121212));
  textEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
  textEditor.setColour(juce::TextEditor::outlineColourId,
                       juce::Colour(0xff333333));
}

TranscriptView::~TranscriptView() {}

void TranscriptView::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xff1e1e1e));

  if (!isConnected) {
    g.setColour(juce::Colours::red);
    g.drawRect(getLocalBounds(), 1);
  }
}

void TranscriptView::resized() {
  textEditor.setBounds(getLocalBounds().reduced(5));
}

void TranscriptView::onTranscriptionReceived(const std::string &text) {
  // Update on message thread
  juce::MessageManager::callAsync([this, text]() {
    textEditor.setText(text);
    textEditor.moveCaretToEnd(); // Auto-scroll
  });
}

void TranscriptView::onStatusChanged(bool connected) {
  juce::MessageManager::callAsync([this, connected]() {
    isConnected = connected;
    if (connected) {
      textEditor.setText("Connected to Engine.\n");
    } else {
      textEditor.setText("Disconnected from Engine.\n");
    }
    repaint();
  });
}

} // namespace punch2pen
