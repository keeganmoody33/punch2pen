#include "TranscriptionCoordinator.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace punch2pen {

TranscriptionCoordinator::TranscriptionCoordinator(IPCServerInterface &ipcServerRef,
                                                   TranscriberInterface &transcriberRef,
                                                   ProfileService &profilesRef)
    : ipcServer(ipcServerRef), transcriber(transcriberRef),
      profiles(profilesRef) {}

void TranscriptionCoordinator::run() {
  running.store(true);

  while (running.load()) {
    bool didWork = false;
    if (ipcServer.hasPendingAudio()) {
      auto block = ipcServer.popAudio();
      if (!block.empty()) {
        const double sampleRate = ipcServer.lastAudioSampleRate();
        if (sampleRate > 0.0) {
          transcriber.setInputSampleRate(sampleRate);
        }
        double dawSampleTime = ipcServer.lastAudioDawSampleTime();
        transcriber.pushAudioBlock(block.data(), static_cast<int>(block.size()),
                                   dawSampleTime,
                                   ipcServer.lastAudioCaptureEpoch());
      }
      didWork = true;
    } else if (ipcServer.transportStateChangedToStop()) {
      transcriber.finalizeStream();
      didWork = true;
    }

    bool correctionApplied = false;
    while (ipcServer.hasPendingCorrection()) {
      auto correction = ipcServer.popCorrection();
      profiles.recordCorrection(correction.original, correction.corrected);
      correctionApplied = true;
      didWork = true;
    }

    while (ipcServer.hasPendingProfileCommand()) {
      profiles.postCommand(ipcServer.popProfileCommand());
      didWork = true;
    }

    // Corrections, profile switches, and cloud refreshes all land here: the
    // account layer bumps its revision and the bias is reapplied once.
    const uint64_t revision = profiles.dictionaryRevision();
    if (revision != appliedRevision) {
      const auto vocab = profiles.vocabularyForBias();
      transcriber.setVocabularyBias(vocab);
      appliedRevision = revision;
      if (correctionApplied)
        std::cout << "Applied correction. Vocabulary terms: " << vocab.size()
                  << std::endl;
      didWork = true;
    }

    if (!didWork)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void TranscriptionCoordinator::stop() { running.store(false); }

} // namespace punch2pen
