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

  punch2pen::Transcriber transcriber;
  std::string modelPath = dataDir + "/models/ggml-base.bin";
  if (!transcriber.initialize(modelPath)) {
    std::cerr << "CRITICAL: Failed to load model at " << modelPath << std::endl;
    // We don't exit here so the process stays alive for IPC, but transcription
    // wont work
  }
  transcriber.setInitialPrompt(dictionary.getInitialPrompt());

  std::cout << "Engine ready." << std::endl;

  while (true) {
    // 1. Check for audio from network
    if (server.hasPendingAudio()) {
      auto packet = server.popAudio();
      transcriber.pushAudio(packet.samples, packet.sampleRate);
    }

    // 2. Run Transcription
    std::string result = transcriber.process();
    if (!result.empty()) {
      std::cout << "Transcription: " << result << std::endl;
      server.sendResult(result);
    }

    // 3. Check for incoming Correction messages
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
