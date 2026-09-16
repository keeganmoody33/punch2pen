#include "Transcriber.h"
#include "TranscriptTiming.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

namespace punch2pen {

Transcriber::Transcriber(const std::string &modelPath) {
  params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  params.print_progress = false;
  params.print_special = false;
  params.print_realtime = false;
  params.print_timestamps = false;
  params.token_timestamps = true;
  params.translate = false;
  params.language = "en";
  params.n_threads = 4;

  struct whisper_context_params cparams = whisper_context_default_params();
  ctx = whisper_init_from_file_with_params(modelPath.c_str(), cparams);
  if (!ctx) {
    std::cerr << "Failed to initialize whisper context from " << modelPath
              << std::endl;
    return;
  }

  std::cout << "Transcriber initialized with model: " << modelPath << std::endl;
}

Transcriber::~Transcriber() {
  if (ctx) {
    whisper_free(ctx);
  }
}

void Transcriber::addListener(Listener *newListener) {
  std::lock_guard<std::mutex> lock(listenerMutex);
  if (std::find(listeners.begin(), listeners.end(), newListener) ==
      listeners.end()) {
    listeners.push_back(newListener);
  }
}

void Transcriber::removeListener(Listener *listenerToRemove) {
  std::lock_guard<std::mutex> lock(listenerMutex);
  listeners.erase(std::remove(listeners.begin(), listeners.end(), listenerToRemove),
                  listeners.end());
}

void Transcriber::setVocabularyBias(const std::vector<std::string> &words) {
  std::string prompt;
  for (const auto &word : words) {
    if (!prompt.empty()) {
      prompt += ", ";
    }
    prompt += word;
  }

  setInitialPrompt(prompt);
}

void Transcriber::finalizeStream() { processAvailableAudio(true); }

bool Transcriber::isReady() const { return ctx != nullptr; }

void Transcriber::setInputSampleRate(double sampleRate) {
  if (sampleRate > 0.0) {
    inputSampleRate = sampleRate;
  }
}

void Transcriber::pushAudioBlock(const float *samples, int sampleCount,
                                 double dawSampleTime) {
  if (samples == nullptr || sampleCount <= 0) {
    return;
  }

  if (audioBuffer.empty()) {
    bufferStartDawSample = dawSampleTime;
  }

  appendResampled(samples, sampleCount);
  processAvailableAudio();
}

void Transcriber::appendResampled(const float *samples, int sampleCount) {
  constexpr double kWhisperRate = 16000.0;
  if (inputSampleRate <= 0.0) {
    return;
  }

  if (std::abs(inputSampleRate - kWhisperRate) < 0.5) {
    audioBuffer.insert(audioBuffer.end(), samples, samples + sampleCount);
    return;
  }

  const double step = inputSampleRate / kWhisperRate;
  double pos = resampleCarry;
  while (pos < static_cast<double>(sampleCount)) {
    const int index = static_cast<int>(pos);
    const int nextIndex = std::min(index + 1, sampleCount - 1);
    const float frac = static_cast<float>(pos - static_cast<double>(index));
    audioBuffer.push_back(samples[index] * (1.0f - frac) +
                          samples[nextIndex] * frac);
    pos += step;
  }
  resampleCarry = pos - static_cast<double>(sampleCount);
}

void Transcriber::setInitialPrompt(const std::string &prompt) {
  currentPrompt = prompt;
  params.initial_prompt = currentPrompt.empty() ? nullptr : currentPrompt.c_str();
}

void Transcriber::processAvailableAudio(bool force) {
  if (!ctx) {
    return;
  }

  if (audioBuffer.empty() ||
      (!force && audioBuffer.size() < WHISPER_SAMPLE_RATE * 3)) {
    return;
  }

  if (whisper_full(ctx, params, audioBuffer.data(),
                   static_cast<int>(audioBuffer.size())) != 0) {
    std::cerr << "Failed to process audio" << std::endl;
    audioBuffer.clear();
    resampleCarry = 0.0;
    return;
  }

  audioBuffer.clear();
  resampleCarry = 0.0;
  emitWordsFromWhisper();
}

void Transcriber::emitWordsFromWhisper() {
  const int n_segments = whisper_full_n_segments(ctx);
  for (int i = 0; i < n_segments; ++i) {
    const double segmentStart = whisperCentisecondsToDawSamples(
        bufferStartDawSample, whisper_full_get_segment_t0(ctx, i),
        inputSampleRate);
    const double segmentEnd = whisperCentisecondsToDawSamples(
        bufferStartDawSample, whisper_full_get_segment_t1(ctx, i),
        inputSampleRate);

    std::vector<TimedWord> tokenWords;
    std::string currentWord;
    double wordStartCs = -1.0;
    double wordEndCs = -1.0;
    bool anyTokenTime = false;

    const int n_tokens = whisper_full_n_tokens(ctx, i);
    auto flushTokenWord = [&]() {
      if (currentWord.empty())
        return;
      TimedWord word;
      word.text = currentWord;
      if (wordStartCs >= 0.0 && wordEndCs >= 0.0) {
        word.startSample = whisperCentisecondsToDawSamples(
            bufferStartDawSample, static_cast<long long>(wordStartCs),
            inputSampleRate);
        word.endSample = whisperCentisecondsToDawSamples(
            bufferStartDawSample, static_cast<long long>(wordEndCs),
            inputSampleRate);
        anyTokenTime = true;
      } else {
        word.startSample = segmentStart;
        word.endSample = segmentEnd;
      }
      tokenWords.push_back(std::move(word));
      currentWord.clear();
      wordStartCs = -1.0;
      wordEndCs = -1.0;
    };

    for (int j = 0; j < n_tokens; ++j) {
      if (whisper_full_get_token_id(ctx, i, j) >= whisper_token_eot(ctx))
        continue;

      const char *tok = whisper_full_get_token_text(ctx, i, j);
      std::string tokenText = tok != nullptr ? tok : "";
      const whisper_token_data data = whisper_full_get_token_data(ctx, i, j);

      bool startsNewWord = currentWord.empty();
      if (!tokenText.empty() &&
          std::isspace(static_cast<unsigned char>(tokenText.front())) != 0) {
        startsNewWord = true;
      }

      size_t firstNonSpace = 0;
      while (firstNonSpace < tokenText.size() &&
             std::isspace(static_cast<unsigned char>(
                 tokenText[firstNonSpace])) != 0) {
        ++firstNonSpace;
      }
      tokenText.erase(0, firstNonSpace);
      while (!tokenText.empty() &&
             std::isspace(static_cast<unsigned char>(tokenText.back())) != 0) {
        tokenText.pop_back();
      }
      if (tokenText.empty())
        continue;

      if (startsNewWord)
        flushTokenWord();

      if (currentWord.empty() && data.t0 >= 0)
        wordStartCs = static_cast<double>(data.t0);
      if (data.t1 >= 0)
        wordEndCs = static_cast<double>(data.t1);
      currentWord += tokenText;
    }
    flushTokenWord();

    const std::vector<TimedWord> words =
        anyTokenTime ? tokenWords
                     : splitWordsAcrossRange(
                           whisper_full_get_segment_text(ctx, i)
                               ? whisper_full_get_segment_text(ctx, i)
                               : "",
                           segmentStart, segmentEnd);

    for (const auto &word : words) {
      if (!word.text.empty())
        notifyListeners(word.text, false, word.startSample, word.endSample);
    }
  }
}

void Transcriber::notifyListeners(const std::string &text, bool isProvisional,
                                 double startTime, double endTime) {
  std::lock_guard<std::mutex> lock(listenerMutex);
  for (auto *listener : listeners) {
    if (listener != nullptr) {
      listener->onTranscriptUpdated(text, isProvisional, startTime, endTime);
    }
  }
}

} // namespace punch2pen
