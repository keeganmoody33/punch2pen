#include "IPCServer.h"
#include "../../shared/Protocol.h"
#include <cstring>
#include <iostream>

namespace punch2pen {

IPCServer::IPCServer(int port) : port(port) {}

IPCServer::~IPCServer() { stop(); }

void IPCServer::start() {
  if (running)
    return;

  serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket < 0) {
    std::cerr << "Failed to create socket" << std::endl;
    return;
  }

  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(serverSocket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    std::cerr << "Failed to bind socket on port " << port << std::endl;
    return;
  }

  listen(serverSocket, 5);

  running = true;
  acceptThread = std::thread(&IPCServer::acceptLoop, this);
  std::cout << "IPC Server started on port " << port << std::endl;
}

void IPCServer::stop() {
  running = false;
  if (serverSocket >= 0) {
    close(serverSocket);
    serverSocket = -1;
  }
  if (acceptThread.joinable()) {
    acceptThread.join();
  }
}

bool IPCServer::hasPendingAudio() {
  std::lock_guard<std::mutex> lock(audioQueueLock);
  return !audioQueue.empty();
}

IPCServer::AudioPacket IPCServer::popAudio() {
  std::lock_guard<std::mutex> lock(audioQueueLock);
  if (audioQueue.empty())
    return {};

  AudioPacket packet = std::move(audioQueue.front());
  audioQueue.erase(audioQueue.begin());
  return packet;
}

void IPCServer::acceptLoop() {
  while (running) {
    sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientSocket =
        accept(serverSocket, (struct sockaddr *)&clientAddr, &clientLen);

    if (clientSocket >= 0) {
      std::cout << "Client connected!" << std::endl;
      std::thread(&IPCServer::clientHandler, this, clientSocket).detach();
    }
  }
}

void IPCServer::clientHandler(int clientSocket) {
  {
    std::lock_guard<std::mutex> lock(clientLock);
    activeClientSocket = clientSocket;
  }

  while (running) {
    protocol::Header header;
    ssize_t bytesRead =
        recv(clientSocket, &header, sizeof(header), MSG_WAITALL);

    if (bytesRead != sizeof(header))
      break;

    if (header.type == protocol::MessageType::AudioChunk) {
      protocol::AudioChunkHeader chunkHeader;
      if (recv(clientSocket, &chunkHeader, sizeof(chunkHeader), MSG_WAITALL) ==
          sizeof(chunkHeader)) {

        std::vector<float> samples(chunkHeader.numSamples);
        size_t payloadSize = chunkHeader.numSamples * sizeof(float);

        if (payloadSize + sizeof(chunkHeader) != header.length) {
          std::cerr << "Protocol mismatch" << std::endl;
        }

        if (recv(clientSocket, samples.data(), payloadSize, MSG_WAITALL) ==
            (ssize_t)payloadSize) {
          std::lock_guard<std::mutex> lock(audioQueueLock);
          audioQueue.push_back({std::move(samples), chunkHeader.sampleRate});
        }
      }
    } else {
      if (header.length > 0) {
        std::vector<char> trash(header.length);
        recv(clientSocket, trash.data(), header.length, MSG_WAITALL);
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(clientLock);
    if (activeClientSocket == clientSocket)
      activeClientSocket = -1;
  }
  close(clientSocket);
}

void IPCServer::sendResult(const std::string &text) {
  std::lock_guard<std::mutex> lock(clientLock);
  if (activeClientSocket < 0)
    return;

  protocol::Header header;
  header.type = protocol::MessageType::TranscriptionResult;

  protocol::TranscriptionResultHeader resultHeader;
  resultHeader.textLength = (uint32_t)text.size();
  resultHeader.startTime = 0.0;
  resultHeader.endTime = 0.0;

  header.length = sizeof(resultHeader) + resultHeader.textLength;

  send(activeClientSocket, &header, sizeof(header), 0);
  send(activeClientSocket, &resultHeader, sizeof(resultHeader), 0);
  send(activeClientSocket, text.data(), text.size(), 0);
}
} // namespace punch2pen
