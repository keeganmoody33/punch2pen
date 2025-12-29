#pragma once

#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

// Platform specific sockets would go here.
// For this MVP we'll use a simulation or simple socket wrapper if portability
// is needed. Since we are writing raw C++ for engine without JUCE, we need
// system sockets. Assume POSIX for macOS for now.

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace punch2pen {

class IPCServer {
public:
  IPCServer(int port = 7483);
  ~IPCServer();

  void start();
  void stop();

  // Polling for new audio
  struct AudioPacket {
    std::vector<float> samples;
    double sampleRate = 48000;
  };

  bool hasPendingAudio();
  AudioPacket popAudio();

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
  std::vector<AudioPacket> audioQueue;
};
} // namespace punch2pen
