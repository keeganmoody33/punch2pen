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
  explicit IPCServer(int port = 7483);
  ~IPCServer() override;

  void start();
  void stop();

  bool hasPendingAudio() override;
  std::vector<float> popAudio() override;
  double lastAudioDawSampleTime() override;
  bool transportStateChangedToStop() override;

  bool hasPendingCorrection() override;
  CorrectionPair popCorrection() override;

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
  struct AudioPacket {
    std::vector<float> samples;
    double dawSampleTime = 0.0;
  };
  std::vector<AudioPacket> audioQueue;
  double lastDawSampleTime_ = 0.0;

  std::mutex correctionQueueLock;
  std::vector<CorrectionPair> correctionQueue;

  std::atomic<bool> transportStopTriggered{false};
};

} // namespace punch2pen
