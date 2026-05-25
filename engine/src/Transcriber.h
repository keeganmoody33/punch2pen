#pragma once

#include <iostream>
#include <string>
#include <vector>

#include "TranscriberInterface.h"
#include "whisper.h"

namespace punch2pen {

class Transcriber : public TranscriberInterface {
public:
  explicit Transcriber(const std::string &modelPath);
  ~Transcriber();

  void pushAudioBlock(const float *samples, int sampleCount,
                      double dawSampleTime) override;

  void setListener(Listener *newListener) override;

  void setInputSampleRate(double sampleRate);

  void setInitialPrompt(const std::string &prompt);

private:
  void processAvailableAudio();

  std::string currentPrompt;
  std::vector<float> audioBuffer;
  struct whisper_context *ctx = nullptr;
  struct whisper_full_params params;
  double inputSampleRate = 16000.0;
  Listener *listener = nullptr;
};

} // namespace punch2pen
