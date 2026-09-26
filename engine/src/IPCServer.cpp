#include "IPCServer.h"
#include "../../shared/Protocol.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/time.h>

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

bool discardPayload(int clientSocket, uint32_t length, uint32_t maxBytes) {
  if (length == 0)
    return true;
  if (length > maxBytes)
    return false;
  std::vector<char> trash(length);
  return recvExact(clientSocket, trash.data(), trash.size());
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
  // On macOS, shutdown() of a listening socket does not reliably wake
  // accept(). close() before join does. The fd is cleared here so stop()
  // cannot close it a second time.
  if (serverSocket >= 0) {
    shutdown(serverSocket, SHUT_RDWR);
    close(serverSocket);
    serverSocket = -1;
  }
  if (acceptThread.joinable())
    acceptThread.join();

  std::vector<std::thread> toJoin;
  {
    std::lock_guard<std::mutex> lock(clientLock);
    for (const int clientSocket : liveSockets)
      shutdown(clientSocket, SHUT_RDWR);
    if (activeClientSocket >= 0)
      shutdown(activeClientSocket, SHUT_RDWR);
    toJoin.swap(clientThreads);
  }
  for (std::thread &worker : toJoin) {
    if (worker.joinable())
      worker.join();
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

bool IPCServer::hasPendingProfileCommand() {
  std::lock_guard<std::mutex> lock(profileQueueLock);
  return !profileCommandQueue.empty();
}

std::string IPCServer::popProfileCommand() {
  std::lock_guard<std::mutex> lock(profileQueueLock);
  if (profileCommandQueue.empty())
    return {};
  std::string command = std::move(profileCommandQueue.front());
  profileCommandQueue.erase(profileCommandQueue.begin());
  return command;
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
      timeval recvBudget{};
      recvBudget.tv_sec = 2;
      recvBudget.tv_usec = 0;
      setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &recvBudget,
                 sizeof(recvBudget));
      std::cout << "Client connected!" << std::endl;
      std::lock_guard<std::mutex> lock(clientLock);
      liveSockets.push_back(clientSocket);
      clientThreads.emplace_back(&IPCServer::clientHandler, this, clientSocket);
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

bool IPCServer::readAndAcknowledgeHandshake(int clientSocket,
                                            uint32_t payloadLength,
                                            bool &handshook) {
  uint32_t accepted = 0;
  if (payloadLength >= sizeof(protocol::Handshake) &&
      payloadLength <= protocol::kMaxJsonPayloadBytes) {
    std::vector<char> payload(payloadLength);
    if (!recvExact(clientSocket, payload.data(), payload.size()))
      return false;
    protocol::Handshake handshake{};
    std::memcpy(&handshake, payload.data(), sizeof(handshake));
    accepted = handshake.version == protocol::kProtocolVersion ? 1u : 0u;
  } else if (payloadLength > protocol::kMaxJsonPayloadBytes) {
    return false;
  } else if (payloadLength > 0) {
    if (!discardPayload(clientSocket, payloadLength,
                        protocol::kMaxJsonPayloadBytes))
      return false;
  }

  std::lock_guard<std::mutex> lock(clientLock);
  if (!sendHandshakeResponse(clientSocket, protocol::kProtocolVersion,
                             accepted))
    return false;
  if (accepted == 0)
    return true;

  handshook = true;
  if (std::find(handshakenClients.begin(), handshakenClients.end(),
                clientSocket) == handshakenClients.end())
    handshakenClients.push_back(clientSocket);
  activeClientSocket = clientSocket;
  return true;
}

void IPCServer::clientHandler(int clientSocket) {
  bool handshook = false;
  constexpr uint32_t kMaxAudioBytes = 8u * 1024u * 1024u;

  while (running) {
    protocol::Header header{};
    if (!recvExact(clientSocket, &header, sizeof(header)))
      break;

    if (header.type == protocol::MessageType::Handshake) {
      if (!readAndAcknowledgeHandshake(clientSocket, header.length, handshook))
        break;
      continue;
    }

    if (!handshook) {
      std::cerr << "Ignoring pre-handshake message, type "
                << static_cast<uint32_t>(header.type) << std::endl;
      if (!discardPayload(clientSocket, header.length,
                          protocol::kMaxJsonPayloadBytes))
        break;
      continue;
    }

    if (header.type == protocol::MessageType::AudioChunk) {
      if (header.length < sizeof(protocol::AudioChunkHeader) ||
          header.length > kMaxAudioBytes) {
        std::cerr << "Protocol mismatch" << std::endl;
        if (header.length > kMaxAudioBytes ||
            !discardPayload(clientSocket, header.length, kMaxAudioBytes))
          break;
        continue;
      }

      protocol::AudioChunkHeader chunkHeader{};
      if (!recvExact(clientSocket, &chunkHeader, sizeof(chunkHeader)))
        break;

      const uint32_t sampleBytes =
          header.length - static_cast<uint32_t>(sizeof(chunkHeader));
      const bool sizeOk =
          chunkHeader.numSamples <= kMaxAudioBytes / sizeof(float) &&
          chunkHeader.numSamples * sizeof(float) == sampleBytes;
      if (!sizeOk) {
        std::cerr << "Protocol mismatch" << std::endl;
        if (!discardPayload(clientSocket, sampleBytes, kMaxAudioBytes))
          break;
        continue;
      }

      std::vector<float> samples(chunkHeader.numSamples);
      if (!recvExact(clientSocket, samples.data(), sampleBytes))
        break;
      std::lock_guard<std::mutex> lock(audioQueueLock);
      eventQueue.push_back({false, std::move(samples), chunkHeader.dawSampleTime,
                            chunkHeader.sampleRate, chunkHeader.captureEpoch});
    } else if (header.type == protocol::MessageType::Correction) {
      if (header.length < sizeof(protocol::CorrectionHeader) ||
          header.length > protocol::kMaxJsonPayloadBytes) {
        std::cerr << "Protocol mismatch" << std::endl;
        if (header.length > protocol::kMaxJsonPayloadBytes ||
            !discardPayload(clientSocket, header.length,
                            protocol::kMaxJsonPayloadBytes))
          break;
        continue;
      }

      protocol::CorrectionHeader corrHeader{};
      if (!recvExact(clientSocket, &corrHeader, sizeof(corrHeader)))
        break;

      const uint32_t body =
          header.length - static_cast<uint32_t>(sizeof(corrHeader));
      const bool sizeOk =
          corrHeader.originalLength <= protocol::kMaxJsonPayloadBytes &&
          corrHeader.correctedLength <= protocol::kMaxJsonPayloadBytes &&
          corrHeader.originalLength + corrHeader.correctedLength == body;
      if (!sizeOk) {
        std::cerr << "Protocol mismatch" << std::endl;
        if (!discardPayload(clientSocket, body, protocol::kMaxJsonPayloadBytes))
          break;
        continue;
      }

      std::string original(corrHeader.originalLength, '\0');
      std::string corrected(corrHeader.correctedLength, '\0');
      if ((corrHeader.originalLength > 0 &&
           !recvExact(clientSocket, original.data(),
                      corrHeader.originalLength)) ||
          (corrHeader.correctedLength > 0 &&
           !recvExact(clientSocket, corrected.data(),
                      corrHeader.correctedLength)))
        break;

      {
        std::lock_guard<std::mutex> lock(correctionQueueLock);
        correctionQueue.push_back({original, corrected});
      }
      std::cout << "Received Correction: '" << original << "' -> '" << corrected
                << "'" << std::endl;
    } else if (header.type == protocol::MessageType::TransportStop) {
      protocol::TransportStopHeader stopHeader{};
      if (header.length >= sizeof(stopHeader)) {
        if (!recvExact(clientSocket, &stopHeader, sizeof(stopHeader)))
          break;
        const uint32_t extra =
            header.length - static_cast<uint32_t>(sizeof(stopHeader));
        if (extra > 0 &&
            !discardPayload(clientSocket, extra, protocol::kMaxJsonPayloadBytes))
          break;
      } else if (!discardPayload(clientSocket, header.length,
                                 protocol::kMaxJsonPayloadBytes)) {
        break;
      }
      std::lock_guard<std::mutex> lock(audioQueueLock);
      eventQueue.push_back({true, {}, 0.0, 0.0, stopHeader.captureEpoch});
    } else if (header.type == protocol::MessageType::ProfileCommand) {
      if (header.length == 0 ||
          header.length > protocol::kMaxJsonPayloadBytes) {
        std::cerr << "Rejecting ProfileCommand of " << header.length
                  << " bytes" << std::endl;
        break;
      }
      std::string payload(header.length, '\0');
      if (!recvExact(clientSocket, payload.data(), header.length))
        break;
      std::lock_guard<std::mutex> lock(profileQueueLock);
      profileCommandQueue.push_back(std::move(payload));
    } else if (!discardPayload(clientSocket, header.length,
                               protocol::kMaxJsonPayloadBytes)) {
      break;
    }
  }

  {
    std::lock_guard<std::mutex> lock(clientLock);
    if (activeClientSocket == clientSocket)
      activeClientSocket = -1;
    handshakenClients.erase(std::remove(handshakenClients.begin(),
                                        handshakenClients.end(), clientSocket),
                            handshakenClients.end());
    liveSockets.erase(
        std::remove(liveSockets.begin(), liveSockets.end(), clientSocket),
        liveSockets.end());
  }
  close(clientSocket);
}

void IPCServer::sendJsonMessage(int clientSocket, uint32_t type,
                                const std::string &json) {
  protocol::Header header{};
  header.type = static_cast<protocol::MessageType>(type);
  header.length = static_cast<uint32_t>(json.size());
  if (!sendExact(clientSocket, &header, sizeof(header)))
    return;
  sendExact(clientSocket, json.data(), json.size());
}

void IPCServer::sendProfileStatus(const std::string &json) {
  if (json.empty() || json.size() > protocol::kMaxJsonPayloadBytes)
    return;
  std::lock_guard<std::mutex> lock(clientLock);
  for (const int clientSocket : handshakenClients)
    sendJsonMessage(clientSocket,
                    static_cast<uint32_t>(protocol::MessageType::ProfileStatus),
                    json);
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

  if (!sendExact(activeClientSocket, &header, sizeof(header)) ||
      !sendExact(activeClientSocket, &resultHeader, sizeof(resultHeader)))
    return;
  if (!text.empty())
    sendExact(activeClientSocket, text.data(), text.size());
}
} // namespace punch2pen
