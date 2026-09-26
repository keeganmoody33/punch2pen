#include "IPCServer.h"
#include "Protocol.h"

#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

int sendFlags() {
#if defined(MSG_NOSIGNAL)
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
}

bool sendAll(int fd, const void *data, size_t nbytes) {
  const auto *p = static_cast<const char *>(data);
  size_t sent = 0;
  while (sent < nbytes) {
    const ssize_t n = ::send(fd, p + sent, nbytes - sent, sendFlags());
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

bool recvAll(int fd, void *data, size_t nbytes) {
  auto *p = static_cast<char *>(data);
  size_t got = 0;
  while (got < nbytes) {
    const ssize_t n = ::recv(fd, p + got, nbytes - got, 0);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

int connectLocal(int port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return -1;
  }
  struct timeval tv {};
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  return fd;
}

bool sendHandshake(int fd, uint32_t version) {
  punch2pen::protocol::Header header{};
  header.type = punch2pen::protocol::MessageType::Handshake;
  punch2pen::protocol::Handshake handshake{};
  handshake.version = version;
  header.length = static_cast<uint32_t>(sizeof(handshake));
  return sendAll(fd, &header, sizeof(header)) &&
         sendAll(fd, &handshake, sizeof(handshake));
}

// Reads the next framed message. Returns false on timeout or a dropped socket.
bool readMessage(int fd, punch2pen::protocol::Header &header,
                 std::vector<char> &payload) {
  if (!recvAll(fd, &header, sizeof(header)))
    return false;
  payload.assign(header.length, 0);
  if (header.length == 0)
    return true;
  return recvAll(fd, payload.data(), header.length);
}

bool handshakeAccepted(const punch2pen::protocol::Header &header,
                       const std::vector<char> &payload) {
  if (header.type != punch2pen::protocol::MessageType::HandshakeResponse ||
      payload.size() != sizeof(punch2pen::protocol::HandshakeResponse))
    return false;
  punch2pen::protocol::HandshakeResponse response{};
  std::memcpy(&response, payload.data(), sizeof(response));
  return response.accepted == 1 &&
         response.version == punch2pen::protocol::kProtocolVersion;
}

void testEarlyTranscriptDoesNotPoisonHandshake() {
  constexpr int kPort = 17641;
  punch2pen::IPCServer server(kPort);
  assert(server.start());

  const int fd = connectLocal(kPort);
  assert(fd >= 0);

  // Accept is not a finished handshake. A transcript written in this window
  // used to land on the socket ahead of HandshakeResponse, so the plugin
  // rejected the reply and stayed on WAIT.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (std::chrono::steady_clock::now() < deadline) {
    server.sendResult("early", 0.0, 1.0, 1);
    char probe = 0;
    const ssize_t n = ::recv(fd, &probe, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n > 0) {
      std::cerr << "sendResult wrote to a client that had not finished "
                   "handshake\n";
      assert(false);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  assert(sendHandshake(fd, punch2pen::protocol::kProtocolVersion));
  punch2pen::protocol::Header header{};
  std::vector<char> payload;
  assert(readMessage(fd, header, payload));
  assert(handshakeAccepted(header, payload));

  ::close(fd);
  server.stop();
  std::cout << "[PASS] testEarlyTranscriptDoesNotPoisonHandshake" << std::endl;
}

void testMessageBeforeHandshakeDoesNotDropClient() {
  constexpr int kPort = 17642;
  punch2pen::IPCServer server(kPort);
  assert(server.start());

  const int fd = connectLocal(kPort);
  assert(fd >= 0);

  // Same bytes the plugin sends once it believes the socket is live. Arriving
  // before Handshake used to log "Closing pre-handshake" and drop the client,
  // so Logic never left WAIT.
  const std::string json = R"({"op":"status"})";
  punch2pen::protocol::Header early{};
  early.type = punch2pen::protocol::MessageType::ProfileCommand;
  early.length = static_cast<uint32_t>(json.size());
  assert(sendAll(fd, &early, sizeof(early)));
  assert(sendAll(fd, json.data(), json.size()));
  assert(sendHandshake(fd, punch2pen::protocol::kProtocolVersion));

  punch2pen::protocol::Header header{};
  std::vector<char> payload;
  if (!readMessage(fd, header, payload) || !handshakeAccepted(header, payload)) {
    std::cerr << "engine dropped the client before handshake finished\n";
    assert(false);
  }

  // Socket is still the plugin's connection: a correction after the handshake
  // must be accepted, not answered with a reset.
  const std::string original = "helo";
  const std::string corrected = "hello";
  punch2pen::protocol::CorrectionHeader corr{};
  corr.originalLength = static_cast<uint32_t>(original.size());
  corr.correctedLength = static_cast<uint32_t>(corrected.size());
  punch2pen::protocol::Header corrHeader{};
  corrHeader.type = punch2pen::protocol::MessageType::Correction;
  corrHeader.length = static_cast<uint32_t>(sizeof(corr) + original.size() +
                                            corrected.size());
  assert(sendAll(fd, &corrHeader, sizeof(corrHeader)));
  assert(sendAll(fd, &corr, sizeof(corr)));
  assert(sendAll(fd, original.data(), original.size()));
  assert(sendAll(fd, corrected.data(), corrected.size()));

  bool got = false;
  for (int i = 0; i < 50 && !got; ++i) {
    got = server.hasPendingCorrection();
    if (!got)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(got);
  const auto pair = server.popCorrection();
  assert(pair.original == original);
  assert(pair.corrected == corrected);

  ::close(fd);
  server.stop();
  std::cout << "[PASS] testMessageBeforeHandshakeDoesNotDropClient" << std::endl;
}

void testAudioLengthMismatchDoesNotDesync() {
  constexpr int kPort = 17643;
  punch2pen::IPCServer server(kPort);
  assert(server.start());

  const int fd = connectLocal(kPort);
  assert(fd >= 0);
  assert(sendHandshake(fd, punch2pen::protocol::kProtocolVersion));
  punch2pen::protocol::Header reply{};
  std::vector<char> replyPayload;
  assert(readMessage(fd, reply, replyPayload));
  assert(handshakeAccepted(reply, replyPayload));

  // Chunk declares more payload than numSamples accounts for. The old reader
  // logged "Protocol mismatch" and then consumed numSamples floats, so the
  // next Correction was framed as garbage and the plugin looked offline.
  punch2pen::protocol::AudioChunkHeader chunk{};
  chunk.sampleRate = 48000.0;
  chunk.numSamples = 0;
  chunk.dawSampleTime = 0.0;
  chunk.captureEpoch = 1;
  const uint32_t extra = 4;
  punch2pen::protocol::Header audio{};
  audio.type = punch2pen::protocol::MessageType::AudioChunk;
  audio.length =
      static_cast<uint32_t>(sizeof(chunk) + extra);
  char tail[4] = {1, 2, 3, 4};
  assert(sendAll(fd, &audio, sizeof(audio)));
  assert(sendAll(fd, &chunk, sizeof(chunk)));
  assert(sendAll(fd, tail, sizeof(tail)));

  const std::string original = "vox";
  const std::string corrected = "vox.";
  punch2pen::protocol::CorrectionHeader corr{};
  corr.originalLength = static_cast<uint32_t>(original.size());
  corr.correctedLength = static_cast<uint32_t>(corrected.size());
  punch2pen::protocol::Header corrHeader{};
  corrHeader.type = punch2pen::protocol::MessageType::Correction;
  corrHeader.length = static_cast<uint32_t>(sizeof(corr) + original.size() +
                                            corrected.size());
  assert(sendAll(fd, &corrHeader, sizeof(corrHeader)));
  assert(sendAll(fd, &corr, sizeof(corr)));
  assert(sendAll(fd, original.data(), original.size()));
  assert(sendAll(fd, corrected.data(), corrected.size()));

  bool got = false;
  for (int i = 0; i < 50 && !got; ++i) {
    got = server.hasPendingCorrection();
    if (!got)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!got) {
    std::cerr << "protocol mismatch desynced the socket; correction lost\n";
    assert(false);
  }
  const auto pair = server.popCorrection();
  assert(pair.original == original);
  assert(pair.corrected == corrected);

  ::close(fd);
  server.stop();
  std::cout << "[PASS] testAudioLengthMismatchDoesNotDesync" << std::endl;
}

} // namespace

int main() {
  std::signal(SIGPIPE, SIG_IGN);
  testEarlyTranscriptDoesNotPoisonHandshake();
  testMessageBeforeHandshakeDoesNotDropClient();
  testAudioLengthMismatchDoesNotDesync();
  std::cout << "All IPC handshake tests passed!" << std::endl;
  return 0;
}
