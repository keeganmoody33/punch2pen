#pragma once

#include "TranscriberInterface.h"
#include "TranscriptTiming.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ix {
class WebSocket;
}

namespace punch2pen {

class OpenAICloudTranscriber : public TranscriberInterface {
public:
  explicit OpenAICloudTranscriber(const std::string &apiKey);
  ~OpenAICloudTranscriber() override;

  void addListener(Listener *listener) override;
  void removeListener(Listener *listener) override;
  void setVocabularyBias(const std::vector<std::string> &words) override;
  void pushAudioBlock(const float *samples, int sampleCount,
                      double dawSampleTime) override;
  void setInputSampleRate(double sampleRate) override;
  void finalizeStream() override;

private:
  void connectToOpenAI();
  void appendResampled(const float *samples, int sampleCount);
  void sendPcm(const std::vector<int16_t> &pcmData);
  void flushPendingTranscript(const std::string &doneTranscript = {});
  std::string encodeBase64(const std::vector<int16_t> &pcmData);

  std::string apiKey;
  std::unique_ptr<ix::WebSocket> webSocket;

  std::vector<Listener *> listeners;
  std::mutex listenerMutex;

  std::vector<int16_t> pcmAccumulator;
  std::mutex audioMutex;
  double inputSampleRate = 48000.0;
  double resampleCarry = 0.0;
  CloudDeltaAssembler stream;
  static constexpr int targetSampleRate = 16000;
  const size_t targetChunkSize = 1600;
};

} // namespace punch2pen

namespace Punch2Pen = punch2pen;
