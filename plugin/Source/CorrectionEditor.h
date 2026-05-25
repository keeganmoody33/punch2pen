#pragma once

#include "IPCClient.h"
#include <JuceHeader.h>
#include <string>

namespace punch2pen {

class CorrectionEditor : public juce::Component {
public:
  CorrectionEditor();
  ~CorrectionEditor() override = default;

  void show(const std::string &originalWord, juce::Point<int> position);
  void hide();
  bool isCurrentlyVisible() const;

  void setIPCClient(IPCClient *client);

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  void submitCorrection();

  juce::Label originalLabel;
  juce::TextEditor correctedInput;
  juce::TextButton submitButton{"Apply"};
  juce::TextButton cancelButton{"Cancel"};

  std::string originalText;
  IPCClient *ipcClient = nullptr;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CorrectionEditor)
};

} // namespace punch2pen
