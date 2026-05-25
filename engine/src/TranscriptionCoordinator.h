#pragma once

#include <atomic>

#include "IPCServerInterface.h"
#include "TranscriberInterface.h"

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
  std::atomic<bool> running{true};
};

} // namespace punch2pen

namespace Punch2Pen = punch2pen;
