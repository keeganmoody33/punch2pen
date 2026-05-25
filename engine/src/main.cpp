#include "../../shared/Protocol.h"
#include "DatabaseManager.h"
#include "DictionaryProcessor.h"
#include "IPCServer.h"
#include "Transcriber.h"
#include <chrono>
#include <filesystem>
#include <iostream>
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

  // Setup data directory
  std::string homeDir = getenv("HOME");
  std::string dataDir = homeDir + "/.punch2pen";
  create_directories(dataDir); // C++17

  punch2pen::DatabaseManager db;
  db.initialize(dataDir + "/corrections.csv");

  punch2pen::DictionaryProcessor dictionary(db);

  punch2pen::IPCServer server(7483);
  server.start();

  std::string modelPath = dataDir + "/models/ggml-base.bin";
  punch2pen::Transcriber transcriber(modelPath);
  EngineTranscriberListener transcriberListener(server);
  transcriber.setListener(&transcriberListener);
  transcriber.setInitialPrompt(dictionary.getInitialPrompt());

  std::cout << "Engine ready." << std::endl;

  while (true) {
    // 1. Check for audio from network
    if (server.hasPendingAudio()) {
      auto packet = server.popAudio();
      transcriber.setInputSampleRate(packet.sampleRate);
      transcriber.pushAudioBlock(packet.samples.data(),
                                 static_cast<int>(packet.samples.size()),
                                 0.0);
    }

    // 2. Check for incoming Correction messages
    while (server.hasPendingCorrection()) {
      auto correction = server.popCorrection();
      db.addCorrection(correction.original, correction.corrected);
      dictionary.refresh();
      std::string newPrompt = dictionary.getInitialPrompt();
      transcriber.setInitialPrompt(newPrompt);
      std::cout << "Applied correction. New Prompt: " << newPrompt << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return 0;
}
