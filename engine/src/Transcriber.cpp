#include "Transcriber.h"

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
  if (ctx)
    whisper_free(ctx);
}

void Transcriber::addListener(Listener *newListener) {
  listener = newListener;
}

void Transcriber::removeListener(Listener *listenerToRemove) {
  if (listener == listenerToRemove) {
    listener = nullptr;
  }
}

void Transcriber::setVocabularyBias(const std::vector<std::string> &words) {
  std::string prompt;
  for (const auto &word : words) {
    if (!prompt.empty()) {
      prompt += ", ";
    }
    prompt += word;
  }

  if (!prompt.empty()) {
    setInitialPrompt(prompt);
  }
}

void Transcriber::finalizeStream() {
  processAvailableAudio();
}

void Transcriber::setInputSampleRate(double sampleRate) {
  inputSampleRate = sampleRate;
}

void Transcriber::pushAudioBlock(const float *samples, int sampleCount,
                                 double dawSampleTime) {
  (void)dawSampleTime;
  if (samples == nullptr || sampleCount <= 0) {
    return;
  }

  // Simple resampling if needed (assuming 16kHz for now as required by whisper)
  // In a real app, we should do high-quality resampling here.
  // For now, allow mismatched sample rates but warn or just append.
  // Ideally, the plugin sends 48k and we downsample.
  // We will assume, for this MVP, that we just take the data.
  // Note: Whisper expects 16kHz. If plugin sends 48k, we need to decimate by 3.

  if (inputSampleRate == 48000.0) {
    // Basic decimation 3:1
    audioBuffer.reserve(audioBuffer.size() + static_cast<size_t>(sampleCount) / 3);
    for (int i = 0; i < sampleCount; i += 3) {
      audioBuffer.push_back(samples[i]);
    }
  } else if (inputSampleRate == 16000.0) {
    audioBuffer.insert(audioBuffer.end(), samples, samples + sampleCount);
  } else {
    // Fallback: Just append (expect poor results)
    audioBuffer.insert(audioBuffer.end(), samples, samples + sampleCount);
  }

  processAvailableAudio();
}

void Transcriber::setInitialPrompt(const std::string &prompt) {
  currentPrompt = prompt;
  params.initial_prompt = currentPrompt.c_str();
}

void Transcriber::processAvailableAudio() {
  if (!ctx) {
    return;
  }

  // Wait for 3 seconds of audio before processing to avoid too frequent calls
  if (audioBuffer.size() < WHISPER_SAMPLE_RATE * 3) {
    return;
  }

  if (whisper_full(ctx, params, audioBuffer.data(), audioBuffer.size()) != 0) {
    std::cerr << "Failed to process audio" << std::endl;
    return;
  }

  std::string result;
  const int n_segments = whisper_full_n_segments(ctx);
  for (int i = 0; i < n_segments; ++i) {
    const char *text = whisper_full_get_segment_text(ctx, i);
    result += text;
  }

  // Clear buffer after processing
  // In a continuous stream, we might want to keep some context or overlapping
  // window, but for simplicity, we clear it.
  audioBuffer.clear();

  if (listener && !result.empty()) {
    listener->onTranscriptUpdated(result, false);
  }
}

} // namespace punch2pen
