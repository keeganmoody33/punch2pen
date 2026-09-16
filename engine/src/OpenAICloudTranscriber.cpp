#include "OpenAICloudTranscriber.h"

#include <algorithm>
#include <cmath>
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
      const std::string type = response.value("type", "");
      if (type == "response.audio_transcript.delta") {
        const std::string deltaText = response.value("delta", "");
        std::lock_guard<std::mutex> audioLock(audioMutex);
        stream.addDelta(deltaText);
        return;
      }
      if (type == "response.audio_transcript.done") {
        flushPendingTranscript(response.value("transcript", ""));
        return;
      }
      if (type == "response.done") {
        flushPendingTranscript();
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

void OpenAICloudTranscriber::setInputSampleRate(double sampleRate) {
  if (sampleRate > 0.0) {
    inputSampleRate = sampleRate;
  }
}

void OpenAICloudTranscriber::appendResampled(const float *samples,
                                            int sampleCount) {
  const double targetRate = static_cast<double>(targetSampleRate);
  auto toPcm = [](float sample) {
    sample = std::max(-1.0f, std::min(1.0f, sample));
    return static_cast<int16_t>(sample * 32767.0f);
  };

  if (inputSampleRate <= 0.0) {
    return;
  }

  if (std::abs(inputSampleRate - targetRate) < 0.5) {
    for (int i = 0; i < sampleCount; ++i) {
      pcmAccumulator.push_back(toPcm(samples[i]));
    }
    return;
  }

  const double step = inputSampleRate / targetRate;
  double pos = resampleCarry;
  while (pos < static_cast<double>(sampleCount)) {
    const int index = static_cast<int>(pos);
    const int nextIndex = std::min(index + 1, sampleCount - 1);
    const float frac = static_cast<float>(pos - static_cast<double>(index));
    const float sample =
        samples[index] * (1.0f - frac) + samples[nextIndex] * frac;
    pcmAccumulator.push_back(toPcm(sample));
    pos += step;
  }
  resampleCarry = pos - static_cast<double>(sampleCount);
}

void OpenAICloudTranscriber::sendPcm(const std::vector<int16_t> &pcmData) {
  if (pcmData.empty() || !webSocket)
    return;

  const json audioAppend = {{"type", "input_audio_buffer.append"},
                            {"audio", encodeBase64(pcmData)}};
  webSocket->send(audioAppend.dump());
  stream.noteLivePcmSent(pcmData.size(), inputSampleRate, targetSampleRate);
}

void OpenAICloudTranscriber::flushPendingTranscript(
    const std::string &doneTranscript) {
  std::vector<TimedWord> words;
  uint32_t epoch = 0;
  {
    std::lock_guard<std::mutex> audioLock(audioMutex);
    epoch = stream.committed.active ? stream.committed.epoch : stream.live.epoch;
    words = stream.complete(doneTranscript);
  }
  if (words.empty())
    return;

  std::lock_guard<std::mutex> lock(listenerMutex);
  for (auto *listener : listeners) {
    if (listener == nullptr)
      continue;
    for (const auto &word : words) {
      listener->onTranscriptUpdated(word.text, true, word.startSample,
                                    word.endSample, epoch);
    }
  }
}

void OpenAICloudTranscriber::pushAudioBlock(const float *samples, int sampleCount,
                                            double dawSampleTime,
                                            uint32_t captureEpoch) {
  if (samples == nullptr || sampleCount <= 0) {
    return;
  }

  // Commit the old DAW window before capturing or sending any PCM from the
  // new origin. Leftover samples in pcmAccumulator belong to the old window.
  bool sendCommit = false;
  {
    std::lock_guard<std::mutex> lock(audioMutex);
    if (stream.needsFinalizeForOrigin(dawSampleTime)) {
      if (!pcmAccumulator.empty()) {
        sendPcm(pcmAccumulator);
        pcmAccumulator.clear();
      }
      if (stream.live.active) {
        stream.finalize();
        sendCommit = true;
      }
    }
  }

  if (sendCommit && webSocket) {
    const json commitEvent = {{"type", "input_audio_buffer.commit"}};
    webSocket->send(commitEvent.dump());
  }

  std::lock_guard<std::mutex> lock(audioMutex);
  stream.captureLiveOrigin(dawSampleTime, captureEpoch);
  appendResampled(samples, sampleCount);
  stream.noteHostSamples(sampleCount);

  while (pcmAccumulator.size() >= targetChunkSize) {
    std::vector<int16_t> chunk(
        pcmAccumulator.begin(),
        pcmAccumulator.begin() + static_cast<std::ptrdiff_t>(targetChunkSize));
    pcmAccumulator.erase(pcmAccumulator.begin(),
                         pcmAccumulator.begin() +
                             static_cast<std::ptrdiff_t>(targetChunkSize));
    sendPcm(chunk);
  }
}

void OpenAICloudTranscriber::finalizeStream() {
  if (!webSocket) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(audioMutex);
    if (!pcmAccumulator.empty()) {
      sendPcm(pcmAccumulator);
      pcmAccumulator.clear();
    }
    stream.finalize();
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
