#include "CorrectionEditor.h"

namespace punch2pen {

CorrectionEditor::CorrectionEditor() {
  addChildComponent(originalLabel);
  addChildComponent(correctedInput);
  addChildComponent(submitButton);
  addChildComponent(cancelButton);

  originalLabel.setFont(juce::Font(14.0f, juce::Font::bold));
  originalLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  originalLabel.setJustificationType(juce::Justification::centredLeft);

  correctedInput.setFont(juce::Font(14.0f));
  correctedInput.setColour(juce::TextEditor::backgroundColourId,
                           juce::Colour(0xff3a3a3a));
  correctedInput.setColour(juce::TextEditor::textColourId,
                           juce::Colours::white);
  correctedInput.setReturnKeyStartsNewLine(false);
  correctedInput.onReturnKey = [this] { submitCorrection(); };

  submitButton.setColour(juce::TextButton::buttonColourId,
                         juce::Colour(0xff4a9f4a));
  submitButton.onClick = [this] { submitCorrection(); };

  cancelButton.setColour(juce::TextButton::buttonColourId,
                         juce::Colour(0xff9f4a4a));
  cancelButton.onClick = [this] { hide(); };

  setVisible(false);
}

void CorrectionEditor::show(const std::string &originalWord,
                            juce::Point<int> position) {
  originalText = originalWord;
  originalLabel.setText("Original: " + juce::String(originalWord),
                        juce::dontSendNotification);
  correctedInput.clear();
  correctedInput.setText(juce::String(originalWord));

  setBounds(position.x, position.y, 260, 90);

  originalLabel.setVisible(true);
  correctedInput.setVisible(true);
  submitButton.setVisible(true);
  cancelButton.setVisible(true);
  setVisible(true);

  correctedInput.grabKeyboardFocus();
  correctedInput.selectAll();
}

void CorrectionEditor::hide() {
  setVisible(false);
  originalLabel.setVisible(false);
  correctedInput.setVisible(false);
  submitButton.setVisible(false);
  cancelButton.setVisible(false);
}

bool CorrectionEditor::isCurrentlyVisible() const { return isVisible(); }

void CorrectionEditor::setIPCClient(IPCClient *client) { ipcClient = client; }

void CorrectionEditor::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xff2d2d2d));
  g.setColour(juce::Colour(0xff505050));
  g.drawRect(getLocalBounds(), 1);
}

void CorrectionEditor::resized() {
  auto area = getLocalBounds().reduced(6);
  originalLabel.setBounds(area.removeFromTop(22));
  area.removeFromTop(4);
  correctedInput.setBounds(area.removeFromTop(24));
  area.removeFromTop(4);

  auto buttonArea = area.removeFromTop(24);
  submitButton.setBounds(buttonArea.removeFromLeft(buttonArea.getWidth() / 2).reduced(2, 0));
  cancelButton.setBounds(buttonArea.reduced(2, 0));
}

void CorrectionEditor::submitCorrection() {
  std::string corrected = correctedInput.getText().toStdString();
  if (!corrected.empty() && corrected != originalText && ipcClient != nullptr) {
    ipcClient->sendCorrection(originalText, corrected);
  }
  hide();
}

} // namespace punch2pen
