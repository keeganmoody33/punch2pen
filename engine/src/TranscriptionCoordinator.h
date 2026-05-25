#pragma once

#include "IPCServerInterface.h"
#include "TranscriberInterface.h"

#include <atomic>

namespace punch2pen {

class TranscriptionCoordinator {
public:
  TranscriptionCoordinator(IPCServerInterface &ipcServer,
                           TranscriberInterface &transcriber);

  void run();
  void stop();

private:
  IPCServerInterface &ipcServer;
  TranscriberInterface &transcriber;
  std::atomic<bool> running{false};
};

} // namespace punch2pen

namespace Punch2Pen = punch2pen;
