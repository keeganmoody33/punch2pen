#include "OpenAICloudTranscriber.h"

#include <algorithm>
#include <cstdint>
#include <iostream>

#include <ixwebsocket/IXBase64.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace punch2pen {

OpenAICloudTranscriber::OpenAICloudTranscriber(const std::string &key)
    : apiKey(key) {
  connectToOpenAI();
}

OpenAICloudTranscriber::~OpenAICloudTranscriber() {
  if (webSocket) {
    webSocket->stop();
  }
}

void OpenAICloudTranscriber::addListener(Listener *listener) {
  std::lock_guard<std::mutex> lock(listenerMutex);
  if (std::find(listeners.begin(), listeners.end(), listener) ==
      listeners.end()) {
    listeners.push_back(listener);
  }
}

void OpenAICloudTranscriber::removeListener(Listener *listener) {
  std::lock_guard<std::mutex> lock(listenerMutex);
  listeners.erase(std::remove(listeners.begin(), listeners.end(), listener),
                  listeners.end());
}

void OpenAICloudTranscriber::connectToOpenAI() {
  webSocket = std::make_unique<ix::WebSocket>();

  const std::string url =
      "wss://api.openai.com/v1/realtime?model=gpt-realtime-whisper";
  webSocket->setUrl(url);

  ix::WebSocketHttpHeaders headers;
  headers["Authorization"] = "Bearer " + apiKey;
  headers["OpenAI-Beta"] = "realtime=v2";
  webSocket->setExtraHeaders(headers);

  webSocket->setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
    if (msg->type != ix::WebSocketMessageType::Message) {
      return;
    }

    try {
      const auto response = json::parse(msg->str);
      if (response.value("type", "") == "response.audio_transcript.delta") {
        const std::string deltaText = response.value("delta", "");
        std::lock_guard<std::mutex> lock(listenerMutex);
        for (auto *listener : listeners) {
          if (listener != nullptr) {
            listener->onTranscriptUpdated(deltaText, true);
          }
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "[OpenAICloudTranscriber] JSON parse error: " << e.what()
                << std::endl;
    }
  });

  webSocket->start();
}

void OpenAICloudTranscriber::setVocabularyBias(
    const std::vector<std::string> &words) {
  if (!webSocket) {
    return;
  }

  std::string promptContext = "Session vocabulary context: ";
  for (const auto &word : words) {
    promptContext += word + ", ";
  }

  json sessionUpdate;
  sessionUpdate["type"] = "session.update";
  sessionUpdate["session"]["instructions"] = promptContext;
  sessionUpdate["session"]["turn_detection"] = nullptr;

  webSocket->send(sessionUpdate.dump());
}

void OpenAICloudTranscriber::pushAudioBlock(const float *samples, int sampleCount,
                                            double dawSampleTime) {
  (void)dawSampleTime;
  if (samples == nullptr || sampleCount <= 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(audioMutex);

  const int decimationFactor = inputSampleRate / targetSampleRate;
  for (int i = 0; i < sampleCount; i += decimationFactor) {
    const float sample = std::max(-1.0f, std::min(1.0f, samples[i]));
    pcmAccumulator.push_back(static_cast<int16_t>(sample * 32767.0f));
  }

  while (pcmAccumulator.size() >= targetChunkSize) {
    std::vector<int16_t> chunk(pcmAccumulator.begin(),
                               pcmAccumulator.begin() + targetChunkSize);
    pcmAccumulator.erase(pcmAccumulator.begin(),
                         pcmAccumulator.begin() + targetChunkSize);

    if (webSocket) {
      const json audioAppend = {{"type", "input_audio_buffer.append"},
                                {"audio", encodeBase64(chunk)}};
      webSocket->send(audioAppend.dump());
    }
  }
}

void OpenAICloudTranscriber::finalizeStream() {
  if (!webSocket) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(audioMutex);
    if (!pcmAccumulator.empty()) {
      const json audioAppend = {{"type", "input_audio_buffer.append"},
                                {"audio", encodeBase64(pcmAccumulator)}};
      webSocket->send(audioAppend.dump());
      pcmAccumulator.clear();
    }
  }

  const json commitEvent = {{"type", "input_audio_buffer.commit"}};
  webSocket->send(commitEvent.dump());
}

std::string OpenAICloudTranscriber::encodeBase64(
    const std::vector<int16_t> &pcmData) {
  const auto *bytes = reinterpret_cast<const char *>(pcmData.data());
  const size_t length = pcmData.size() * sizeof(int16_t);
  return macaron::Base64::Encode(std::string(bytes, length));
}

} // namespace punch2pen
