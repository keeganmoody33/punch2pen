#include "TranscriptionCoordinator.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace punch2pen {

TranscriptionCoordinator::TranscriptionCoordinator(IPCServerInterface &ipcServerRef,
                                                   TranscriberInterface &transcriberRef,
                                                   DatabaseManager &dbRef)
    : ipcServer(ipcServerRef), transcriber(transcriberRef), db(dbRef) {}

void TranscriptionCoordinator::run() {
  running.store(true);

  while (running.load()) {
    if (ipcServer.hasPendingAudio()) {
      auto block = ipcServer.popAudio();
      if (!block.empty()) {
        double dawSampleTime = ipcServer.lastAudioDawSampleTime();
        transcriber.pushAudioBlock(block.data(), static_cast<int>(block.size()),
                                   dawSampleTime);
      }
    }

    if (ipcServer.transportStateChangedToStop()) {
      transcriber.finalizeStream();
    }

    while (ipcServer.hasPendingCorrection()) {
      auto correction = ipcServer.popCorrection();
      db.addCorrection(correction.original, correction.corrected);

      auto vocab = db.getVocabulary();
      transcriber.setVocabularyBias(vocab);

      std::cout << "Applied correction. Vocabulary terms: " << vocab.size()
                << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void TranscriptionCoordinator::stop() { running.store(false); }

} // namespace punch2pen
