#include "DatabaseManager.h"
#include "IPCServer.h"
#include "OpenAICloudTranscriber.h"
#include "Transcriber.h"
#include "TranscriberInterface.h"
#include "TranscriptionCoordinator.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>

class EngineTranscriberListener
    : public punch2pen::TranscriberInterface::Listener {
public:
  explicit EngineTranscriberListener(punch2pen::IPCServer &serverRef)
      : server(serverRef) {}

  void onTranscriptUpdated(const std::string &text,
                           bool isProvisional) override {
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

  const char *home = std::getenv("HOME");
  const std::string dataDir =
      std::string(home != nullptr ? home : ".") + "/.punch2pen";
  std::filesystem::create_directories(dataDir);

  punch2pen::DatabaseManager db;
  db.initialize(dataDir + "/corrections.csv");

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
      std::cerr << "Cloud mode requested but no API key supplied via "
                   "--api-key= or OPENAI_API_KEY"
                << std::endl;
      return 1;
    }

    std::cout << "Mode: [ONLINE] OpenAI Realtime" << std::endl;
    cloudTranscriber =
        std::make_unique<punch2pen::OpenAICloudTranscriber>(apiKey);
    activeTranscriber = cloudTranscriber.get();
  } else {
    std::cout << "Mode: [LOCAL] whisper.cpp" << std::endl;
    const std::string modelPath = dataDir + "/models/ggml-base.bin";
    localTranscriber = std::make_unique<punch2pen::Transcriber>(modelPath);
    localTranscriber->setInputSampleRate(48000.0);
    activeTranscriber = localTranscriber.get();
  }

  activeTranscriber->setVocabularyBias(db.getVocabulary());

  EngineTranscriberListener transcriberListener(server);
  activeTranscriber->addListener(&transcriberListener);

  std::cout << "Engine ready." << std::endl;

  punch2pen::TranscriptionCoordinator coordinator(server, *activeTranscriber, db);
  coordinator.run();

  return 0;
}
