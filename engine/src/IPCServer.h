#pragma once

#include "IPCServerInterface.h"

#include <atomic>
#include <cstdint>
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

  // Binds 127.0.0.1 and starts the accept thread. Returns false if the
  // socket cannot listen; the process should exit rather than claim ready.
  bool start();
  void stop();

  bool hasPendingAudio() override;
  std::vector<float> popAudio() override;
  double lastAudioDawSampleTime() override;
  double lastAudioSampleRate() override;
  uint32_t lastAudioCaptureEpoch() override;
  bool transportStateChangedToStop() override;

  bool hasPendingCorrection() override;
  CorrectionPair popCorrection() override;

  bool hasPendingProfileCommand() override;
  std::string popProfileCommand() override;

  void sendResult(const std::string &text, double startTime, double endTime,
                  uint32_t captureEpoch);

  // Broadcasts a ProfileStatus JSON document to every handshaken plugin so
  // each editor's active-profile pill agrees with the engine.
  void sendProfileStatus(const std::string &json);

private:
  void acceptLoop();
  void clientHandler(int clientSocket);
  bool sendHandshakeResponse(int clientSocket, uint32_t version,
                             uint32_t accepted);
  void sendJsonMessage(int clientSocket, uint32_t type,
                       const std::string &json);

  int serverSocket = -1;
  int activeClientSocket = -1;
  std::vector<int> handshakenClients;
  int port;
  std::atomic<bool> running{false};
  std::thread acceptThread;
  std::mutex clientLock;

  std::mutex audioQueueLock;
  struct QueuedEvent {
    bool isStop = false;
    std::vector<float> samples;
    double dawSampleTime = 0.0;
    double sampleRate = 0.0;
    uint32_t captureEpoch = 0;
  };
  std::vector<QueuedEvent> eventQueue;
  double lastDawSampleTime_ = 0.0;
  double lastSampleRate_ = 0.0;
  uint32_t lastCaptureEpoch_ = 0;

  std::mutex correctionQueueLock;
  std::vector<CorrectionPair> correctionQueue;

  std::mutex profileQueueLock;
  std::vector<std::string> profileCommandQueue;
};

} // namespace punch2pen
