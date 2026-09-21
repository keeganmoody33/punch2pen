#pragma once

#include "IPCServerInterface.h"
#include "ProfileService.h"
#include "TranscriberInterface.h"

#include <atomic>
#include <cstdint>

namespace punch2pen {

class TranscriptionCoordinator {
public:
  TranscriptionCoordinator(IPCServerInterface &ipcServer,
                           TranscriberInterface &transcriber,
                           ProfileService &profiles);

  void run();
  void stop();

private:
  IPCServerInterface &ipcServer;
  TranscriberInterface &transcriber;
  ProfileService &profiles;
  uint64_t appliedRevision = 0;
  std::atomic<bool> running{false};
};

} // namespace punch2pen

namespace Punch2Pen = punch2pen;
