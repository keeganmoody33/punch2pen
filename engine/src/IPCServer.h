#pragma once

#include "IPCServerInterface.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace punch2pen {

class IPCServer : public IPCServerInterface {
public:
  struct Correction {
    std::string original;
    std::string corrected;
  };

  explicit IPCServer(int port = 7483);
  ~IPCServer() override;

  void start();
  void stop();

  bool hasPendingAudio() override;
  std::vector<float> popAudio() override;
  bool transportStateChangedToStop() override;

  bool hasPendingCorrection();
  Correction popCorrection();

  void sendResult(const std::string &text);

private:
  void acceptLoop();
  void clientHandler(int clientSocket);

  int serverSocket = -1;
  int activeClientSocket = -1;
  int port;
  std::atomic<bool> running{false};
  std::thread acceptThread;
  std::mutex clientLock;

  std::mutex audioQueueLock;
  std::vector<std::vector<float>> audioQueue;

  std::mutex correctionQueueLock;
  std::vector<Correction> correctionQueue;

  std::atomic<bool> transportStopTriggered{false};
};

} // namespace punch2pen
