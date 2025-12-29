#pragma once

#include <iostream>
#include <string>
#include <vector>

// Placeholders for whisper.cpp includes
// #include "whisper.h"

namespace punch2pen {

class Transcriber {
public:
  Transcriber();
  ~Transcriber();

  bool initialize(const std::string &modelPath);

  // Push audio to the transcription buffer
  void pushAudio(const std::vector<float> &samples, double sampleRate);

  // Run one iteration of inference (blocking)
  // Returns transcribed text segment if any, else empty
  std::string process();

  void setInitialPrompt(const std::string &prompt);

private:
  std::string currentPrompt;
  std::vector<float> audioBuffer;
  // whisper_context* ctx = nullptr;
};

} // namespace punch2pen
