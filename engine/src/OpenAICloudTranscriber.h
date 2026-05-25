#pragma once

#include "TranscriberInterface.h"

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
  void finalizeStream() override;

private:
  void connectToOpenAI();
  std::string encodeBase64(const std::vector<int16_t> &pcmData);

  std::string apiKey;
  std::unique_ptr<ix::WebSocket> webSocket;

  std::vector<Listener *> listeners;
  std::mutex listenerMutex;

  std::vector<int16_t> pcmAccumulator;
  std::mutex audioMutex;
  const size_t targetChunkSize = 1600;
};

} // namespace punch2pen

namespace Punch2Pen = punch2pen;
