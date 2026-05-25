#pragma once

#include "TranscriberInterface.h"
#include "whisper.h"

#include <mutex>
#include <string>
#include <vector>

namespace punch2pen {

class Transcriber : public TranscriberInterface {
public:
  explicit Transcriber(const std::string &modelPath);
  ~Transcriber() override;

  void pushAudioBlock(const float *samples, int sampleCount,
                      double dawSampleTime) override;
  void addListener(Listener *newListener) override;
  void removeListener(Listener *listenerToRemove) override;
  void setVocabularyBias(const std::vector<std::string> &words) override;
  void finalizeStream() override;

  void setInputSampleRate(double sampleRate);
  void setInitialPrompt(const std::string &prompt);

private:
  void processAvailableAudio(bool force = false);
  void notifyListeners(const std::string &text, bool isProvisional);

  std::string currentPrompt;
  std::vector<float> audioBuffer;
  struct whisper_context *ctx = nullptr;
  struct whisper_full_params params;
  double inputSampleRate = 16000.0;

  std::mutex listenerMutex;
  std::vector<Listener *> listeners;
};

} // namespace punch2pen
