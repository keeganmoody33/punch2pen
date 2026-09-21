#include "IPCServer.h"
#include "../../shared/Protocol.h"
#include <cerrno>
#include <cstring>
#include <iostream>

namespace punch2pen {

namespace {

int sendFlags() {
#if defined(MSG_NOSIGNAL)
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
}

void suppressClientSigPipe(int clientSocket) {
#if defined(SO_NOSIGPIPE)
  int disableSigPipe = 1;
  setsockopt(clientSocket, SOL_SOCKET, SO_NOSIGPIPE, &disableSigPipe,
             sizeof(disableSigPipe));
#else
  (void)clientSocket;
#endif
}

bool recvExact(int clientSocket, void *buf, size_t nbytes) {
  auto *p = static_cast<char *>(buf);
  size_t got = 0;
  while (got < nbytes) {
    const ssize_t n = recv(clientSocket, p + got, nbytes - got, 0);
    if (n == 0)
      return false;
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    got += static_cast<size_t>(n);
  }
  return true;
}

bool sendExact(int clientSocket, const void *buf, size_t nbytes) {
  auto *p = static_cast<const char *>(buf);
  size_t sent = 0;
  while (sent < nbytes) {
    const ssize_t n =
        send(clientSocket, p + sent, nbytes - sent, sendFlags());
    if (n <= 0) {
      if (n < 0 && errno == EINTR)
        continue;
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

} // namespace

IPCServer::IPCServer(int port) : port(port) {}

IPCServer::~IPCServer() { stop(); }

bool IPCServer::start() {
  if (running)
    return true;

  serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket < 0) {
    std::cerr << "Failed to create socket" << std::endl;
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port));

  int opt = 1;
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  if (bind(serverSocket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    std::cerr << "Failed to bind socket on 127.0.0.1:" << port << std::endl;
    close(serverSocket);
    serverSocket = -1;
    return false;
  }

  if (listen(serverSocket, 5) < 0) {
    std::cerr << "Failed to listen on 127.0.0.1:" << port << std::endl;
    close(serverSocket);
    serverSocket = -1;
    return false;
  }

  running = true;
  acceptThread = std::thread(&IPCServer::acceptLoop, this);
  std::cout << "IPC Server started on 127.0.0.1:" << port << std::endl;
  return true;
}

void IPCServer::stop() {
  running = false;
  // close() alone does not always wake a blocking accept() on macOS, so
  // SIGTERM then wait() in engine_smoke hung until the 10-minute CI cap.
  if (serverSocket >= 0) {
    shutdown(serverSocket, SHUT_RDWR);
  }
  {
    std::lock_guard<std::mutex> lock(clientLock);
    if (activeClientSocket >= 0) {
      shutdown(activeClientSocket, SHUT_RDWR);
    }
  }
  if (acceptThread.joinable()) {
    acceptThread.join();
  }
  if (serverSocket >= 0) {
    close(serverSocket);
    serverSocket = -1;
  }
}

bool IPCServer::hasPendingAudio() {
  std::lock_guard<std::mutex> lock(audioQueueLock);
  return !eventQueue.empty() && !eventQueue.front().isStop;
}

std::vector<float> IPCServer::popAudio() {
  std::lock_guard<std::mutex> lock(audioQueueLock);
  if (eventQueue.empty() || eventQueue.front().isStop)
    return {};

  auto packet = std::move(eventQueue.front());
  eventQueue.erase(eventQueue.begin());
  lastDawSampleTime_ = packet.dawSampleTime;
  lastSampleRate_ = packet.sampleRate;
  lastCaptureEpoch_ = packet.captureEpoch;
  return std::move(packet.samples);
}

double IPCServer::lastAudioDawSampleTime() {
  return lastDawSampleTime_;
}

double IPCServer::lastAudioSampleRate() {
  return lastSampleRate_;
}

uint32_t IPCServer::lastAudioCaptureEpoch() {
  return lastCaptureEpoch_;
}

bool IPCServer::hasPendingCorrection() {
  std::lock_guard<std::mutex> lock(correctionQueueLock);
  return !correctionQueue.empty();
}

IPCServer::CorrectionPair IPCServer::popCorrection() {
  std::lock_guard<std::mutex> lock(correctionQueueLock);
  if (correctionQueue.empty())
    return {};

  CorrectionPair c = correctionQueue.front();
  correctionQueue.erase(correctionQueue.begin());
  return c;
}

bool IPCServer::transportStateChangedToStop() {
  std::lock_guard<std::mutex> lock(audioQueueLock);
  if (eventQueue.empty() || !eventQueue.front().isStop)
    return false;
  lastCaptureEpoch_ = eventQueue.front().captureEpoch;
  eventQueue.erase(eventQueue.begin());
  return true;
}

void IPCServer::acceptLoop() {
  while (running) {
    sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientSocket =
        accept(serverSocket, (struct sockaddr *)&clientAddr, &clientLen);

    if (clientSocket >= 0) {
      suppressClientSigPipe(clientSocket);
      std::cout << "Client connected!" << std::endl;
      std::thread(&IPCServer::clientHandler, this, clientSocket).detach();
    }
  }
}

bool IPCServer::sendHandshakeResponse(int clientSocket, uint32_t version,
                                      uint32_t accepted) {
  protocol::Header header{};
  header.type = protocol::MessageType::HandshakeResponse;
  protocol::HandshakeResponse response{};
  response.version = version;
  response.accepted = accepted;
  header.length = (uint32_t)sizeof(response);

  return sendExact(clientSocket, &header, sizeof(header)) &&
         sendExact(clientSocket, &response, sizeof(response));
}

void IPCServer::clientHandler(int clientSocket) {
  {
    std::lock_guard<std::mutex> lock(clientLock);
    activeClientSocket = clientSocket;
  }

  bool handshook = false;

  while (running) {
    protocol::Header header;
    if (!recvExact(clientSocket, &header, sizeof(header)))
      break;

    if (header.type == protocol::MessageType::Handshake) {
      protocol::Handshake handshake{};
      if (header.length != sizeof(handshake))
        break;
      if (!recvExact(clientSocket, &handshake, sizeof(handshake)))
        break;
      const uint32_t accepted =
          handshake.version == protocol::kProtocolVersion ? 1u : 0u;
      if (!sendHandshakeResponse(clientSocket, protocol::kProtocolVersion,
                                 accepted))
        break;
      if (accepted == 0)
        break;
      handshook = true;
      continue;
    }

    if (!handshook) {
      std::cerr << "Closing pre-handshake connection, type "
                << static_cast<uint32_t>(header.type) << std::endl;
      break;
    }

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
          eventQueue.push_back({false, std::move(samples),
                                chunkHeader.dawSampleTime,
                                chunkHeader.sampleRate,
                                chunkHeader.captureEpoch});
        }
      }
    } else if (header.type == protocol::MessageType::Correction) {
      protocol::CorrectionHeader corrHeader;
      if (recv(clientSocket, &corrHeader, sizeof(corrHeader), MSG_WAITALL) ==
          sizeof(corrHeader)) {

        std::string original(corrHeader.originalLength, '\0');
        if (recv(clientSocket, original.data(), corrHeader.originalLength,
                 MSG_WAITALL) == (ssize_t)corrHeader.originalLength) {
          std::string corrected(corrHeader.correctedLength, '\0');
          if (recv(clientSocket, corrected.data(), corrHeader.correctedLength,
                   MSG_WAITALL) == (ssize_t)corrHeader.correctedLength) {
            std::lock_guard<std::mutex> lock(correctionQueueLock);
            correctionQueue.push_back({original, corrected});
            std::cout << "Received Correction: '" << original << "' -> '"
                      << corrected << "'" << std::endl;
          }
        }
      }
    } else if (header.type == protocol::MessageType::TransportStop) {
      protocol::TransportStopHeader stopHeader{};
      if (header.length >= sizeof(stopHeader)) {
        if (recv(clientSocket, &stopHeader, sizeof(stopHeader), MSG_WAITALL) !=
            sizeof(stopHeader))
          break;
        if (header.length > sizeof(stopHeader)) {
          std::vector<char> trash(header.length - sizeof(stopHeader));
          recv(clientSocket, trash.data(),
               header.length - sizeof(stopHeader), MSG_WAITALL);
        }
      } else if (header.length > 0) {
        std::vector<char> trash(header.length);
        recv(clientSocket, trash.data(), header.length, MSG_WAITALL);
      }
      std::lock_guard<std::mutex> lock(audioQueueLock);
      eventQueue.push_back(
          {true, {}, 0.0, 0.0, stopHeader.captureEpoch});
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

void IPCServer::sendResult(const std::string &text, double startTime,
                           double endTime, uint32_t captureEpoch) {
  std::lock_guard<std::mutex> lock(clientLock);
  if (activeClientSocket < 0)
    return;

  protocol::Header header;
  header.type = protocol::MessageType::TranscriptionResult;

  protocol::TranscriptionResultHeader resultHeader;
  resultHeader.textLength = (uint32_t)text.size();
  resultHeader.startTime = startTime;
  resultHeader.endTime = endTime;
  resultHeader.captureEpoch = captureEpoch;

  header.length = sizeof(resultHeader) + resultHeader.textLength;

  send(activeClientSocket, &header, sizeof(header), 0);
  send(activeClientSocket, &resultHeader, sizeof(resultHeader), 0);
  send(activeClientSocket, text.data(), text.size(), 0);
}
} // namespace punch2pen
