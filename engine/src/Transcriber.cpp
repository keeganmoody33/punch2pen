#include "Transcriber.h"

namespace punch2pen {

Transcriber::Transcriber() {}

Transcriber::~Transcriber() {
  // if (ctx) whisper_free(ctx);
}

bool Transcriber::initialize(const std::string &modelPath) {
  // ctx = whisper_init_from_file(modelPath.c_str());
  std::cout << "Transcriber initialized (MOCK MODE)" << std::endl;
  return true;
}

void Transcriber::pushAudio(const std::vector<float> &samples,
                            double sampleRate) {
  audioBuffer.insert(audioBuffer.end(), samples.begin(), samples.end());
}

void Transcriber::setInitialPrompt(const std::string &prompt) {
  currentPrompt = prompt;
}

std::string Transcriber::process() {
  if (audioBuffer.size() > 16000 * 2) { // Process if > 2 seconds
    // Mock Transcription
    std::string result =
        "Processed " + std::to_string(audioBuffer.size()) + " samples.";
    if (!currentPrompt.empty()) {
      result += " [Context: " + currentPrompt.substr(0, 10) + "...]";
    }
    audioBuffer.clear(); // Consume
    return result;
  }
  return "";
}

} // namespace punch2pen
