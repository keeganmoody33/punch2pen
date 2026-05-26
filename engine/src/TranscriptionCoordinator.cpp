#include "TranscriptionCoordinator.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <unordered_set>

namespace punch2pen {

TranscriptionCoordinator::TranscriptionCoordinator(IPCServerInterface &ipcServerRef,
                                                   TranscriberInterface &transcriberRef,
                                                   DatabaseManager &dbRef,
                                                   ProfileManager &profileManagerRef)
    : ipcServer(ipcServerRef), transcriber(transcriberRef), db(dbRef),
      profileManager(profileManagerRef) {}

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
      profileManager.addCorrection(correction.original, correction.corrected);

      auto dbVocab = db.getVocabulary();
      auto profileVocab = profileManager.getVocabulary();
      std::unordered_set<std::string> merged(dbVocab.begin(), dbVocab.end());
      merged.insert(profileVocab.begin(), profileVocab.end());
      std::vector<std::string> vocab(merged.begin(), merged.end());
      transcriber.setVocabularyBias(vocab);

      std::cout << "Applied correction. Vocabulary terms: " << vocab.size()
                << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void TranscriptionCoordinator::stop() { running.store(false); }

} // namespace punch2pen
