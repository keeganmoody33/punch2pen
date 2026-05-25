#include "../../shared/Protocol.h"
#include "DatabaseManager.h"
#include "DictionaryProcessor.h"
#include "IPCServer.h"
#include "OpenAICloudTranscriber.h"
#include "Transcriber.h"
#include "TranscriptionCoordinator.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

using std::filesystem::create_directories;

class EngineTranscriberListener : public punch2pen::TranscriberInterface::Listener {
public:
  explicit EngineTranscriberListener(punch2pen::IPCServer &serverRef)
      : server(serverRef) {}

  void onTranscriptUpdated(const std::string &text, bool isProvisional) override {
    (void)isProvisional;
    std::cout << "Transcription: " << text << std::endl;
    server.sendResult(text);
  }

private:
  punch2pen::IPCServer &server;
};

int main(int argc, char *argv[]) {
  std::cout << "punch2pen Engine v1.0.0" << std::endl;

  bool useCloudMode = false;
  std::string apiKey;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--cloud") {
      useCloudMode = true;
    } else if (arg.rfind("--api-key=", 0) == 0) {
      apiKey = arg.substr(10);
    }
  }

  // Setup data directory
  std::string homeDir = getenv("HOME");
  std::string dataDir = homeDir + "/.punch2pen";
  create_directories(dataDir); // C++17

  punch2pen::DatabaseManager db;
  db.initialize(dataDir + "/corrections.csv");

  punch2pen::DictionaryProcessor dictionary(db);

  punch2pen::IPCServer server(7483);
  server.start();

  punch2pen::TranscriberInterface *activeTranscriber = nullptr;
  std::unique_ptr<punch2pen::TranscriberInterface> cloudTranscriber;
  std::unique_ptr<punch2pen::Transcriber> localTranscriber;

  if (useCloudMode) {
    if (apiKey.empty()) {
      const char *envKey = std::getenv("OPENAI_API_KEY");
      if (envKey != nullptr) {
        apiKey = envKey;
      }
    }

    if (apiKey.empty()) {
      std::cerr << "Cloud mode requested but no API key supplied via --api-key="
                   " or OPENAI_API_KEY"
                << std::endl;
      return 1;
    }

    std::cout << "Mode: [ONLINE] OpenAI Realtime" << std::endl;
    cloudTranscriber = std::make_unique<punch2pen::OpenAICloudTranscriber>(apiKey);
    activeTranscriber = cloudTranscriber.get();
  } else {
    std::cout << "Mode: [LOCAL] whisper.cpp" << std::endl;
    std::string modelPath = dataDir + "/models/ggml-base.bin";
    localTranscriber = std::make_unique<punch2pen::Transcriber>(modelPath);
    localTranscriber->setInputSampleRate(48000.0);
    localTranscriber->setInitialPrompt(dictionary.getInitialPrompt());
    activeTranscriber = localTranscriber.get();
  }

  EngineTranscriberListener transcriberListener(server);
  activeTranscriber->addListener(&transcriberListener);

  std::cout << "Engine ready." << std::endl;

  while (true) {
    // 1. Check for audio from network
    if (server.hasPendingAudio()) {
      auto packet = server.popAudio();
      if (localTranscriber) {
        localTranscriber->setInputSampleRate(packet.sampleRate);
      }
      activeTranscriber->pushAudioBlock(packet.samples.data(),
                                        static_cast<int>(packet.samples.size()),
                                        0.0);
    }


    if (server.transportStateChangedToStop()) {
      activeTranscriber->finalizeStream();
    }

    // 2. Check for incoming Correction messages
    while (server.hasPendingCorrection()) {
      auto correction = server.popCorrection();
      db.addCorrection(correction.original, correction.corrected);
      dictionary.refresh();
      std::string newPrompt = dictionary.getInitialPrompt();
      if (localTranscriber) {
        localTranscriber->setInitialPrompt(newPrompt);
      } else {
        activeTranscriber->setVocabularyBias({newPrompt});
      }
      std::cout << "Applied correction. New Prompt: " << newPrompt << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return 0;
}
