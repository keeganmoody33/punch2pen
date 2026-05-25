#include "TranscriptionCoordinator.h"

#include <chrono>
#include <thread>

namespace punch2pen {

TranscriptionCoordinator::TranscriptionCoordinator(IPCServerInterface &ipcServerRef,
                                                   TranscriberInterface &transcriberRef)
    : ipcServer(ipcServerRef), transcriber(transcriberRef) {}

void TranscriptionCoordinator::run() {
  running.store(true);
  while (running.load()) {
    if (ipcServer.hasPendingAudio()) {
      auto block = ipcServer.popAudio();
      if (!block.empty()) {
        transcriber.pushAudioBlock(block.data(), static_cast<int>(block.size()), 0.0);
      }
    }

    if (ipcServer.transportStateChangedToStop()) {
      transcriber.finalizeStream();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void TranscriptionCoordinator::stop() { running.store(false); }

} // namespace punch2pen
