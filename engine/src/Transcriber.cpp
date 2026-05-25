#include "Transcriber.h"

#include <algorithm>
#include <iostream>

namespace punch2pen {

Transcriber::Transcriber(const std::string &modelPath) {
  params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  params.print_progress = false;
  params.print_special = false;
  params.print_realtime = false;
  params.print_timestamps = false;
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

void Transcriber::setInputSampleRate(double sampleRate) {
  inputSampleRate = sampleRate;
}

void Transcriber::pushAudioBlock(const float *samples, int sampleCount,
                                 double dawSampleTime) {
  (void)dawSampleTime;
  if (samples == nullptr || sampleCount <= 0) {
    return;
  }

  if (inputSampleRate == 48000.0) {
    audioBuffer.reserve(audioBuffer.size() + static_cast<size_t>(sampleCount) / 3);
    for (int i = 0; i < sampleCount; i += 3) {
      audioBuffer.push_back(samples[i]);
    }
  } else if (inputSampleRate == 16000.0) {
    audioBuffer.insert(audioBuffer.end(), samples, samples + sampleCount);
  } else {
    audioBuffer.insert(audioBuffer.end(), samples, samples + sampleCount);
  }

  processAvailableAudio();
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
    return;
  }

  std::string result;
  const int n_segments = whisper_full_n_segments(ctx);
  for (int i = 0; i < n_segments; ++i) {
    result += whisper_full_get_segment_text(ctx, i);
  }

  audioBuffer.clear();

  if (!result.empty()) {
    notifyListeners(result, false);
  }
}

void Transcriber::notifyListeners(const std::string &text, bool isProvisional) {
  std::lock_guard<std::mutex> lock(listenerMutex);
  for (auto *listener : listeners) {
    if (listener != nullptr) {
      listener->onTranscriptUpdated(text, isProvisional);
    }
  }
}

} // namespace punch2pen
