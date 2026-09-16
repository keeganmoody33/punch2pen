#pragma once

#include "TranscriberInterface.h"
#include "whisper.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace punch2pen {

class Transcriber : public TranscriberInterface {
public:
  explicit Transcriber(const std::string &modelPath);
  ~Transcriber() override;

  void pushAudioBlock(const float *samples, int sampleCount,
                      double dawSampleTime, uint32_t captureEpoch) override;
  void addListener(Listener *newListener) override;
  void removeListener(Listener *listenerToRemove) override;
  void setVocabularyBias(const std::vector<std::string> &words) override;
  void setInputSampleRate(double sampleRate) override;
  void finalizeStream() override;

  bool isReady() const;
  void setInitialPrompt(const std::string &prompt);

private:
  void processAvailableAudio(bool force = false);
  void notifyListeners(const std::string &text, bool isProvisional,
                       double startTime, double endTime);
  void appendResampled(const float *samples, int sampleCount);
  void emitWordsFromWhisper();

  std::string currentPrompt;
  std::vector<float> audioBuffer;
  struct whisper_context *ctx = nullptr;
  struct whisper_full_params params;
  double inputSampleRate = 16000.0;
  double bufferHostSampleRate = 16000.0;
  double resampleCarry = 0.0;
  double bufferStartDawSample = 0.0;
  int bufferHostSamples = 0;
  uint32_t bufferCaptureEpoch = 0;

  std::mutex listenerMutex;
  std::vector<Listener *> listeners;
};

} // namespace punch2pen
