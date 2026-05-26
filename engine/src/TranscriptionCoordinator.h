#pragma once

#include "DatabaseManager.h"
#include "IPCServerInterface.h"
#include "ProfileManager.h"
#include "TranscriberInterface.h"

#include <atomic>

namespace punch2pen {

class TranscriptionCoordinator {
public:
  TranscriptionCoordinator(IPCServerInterface &ipcServer,
                           TranscriberInterface &transcriber,
                           DatabaseManager &db,
                           ProfileManager &profileManager);

  void run();
  void stop();

private:
  IPCServerInterface &ipcServer;
  TranscriberInterface &transcriber;
  DatabaseManager &db;
  ProfileManager &profileManager;
  std::atomic<bool> running{false};
};

} // namespace punch2pen

namespace Punch2Pen = punch2pen;
