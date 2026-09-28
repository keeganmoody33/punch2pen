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

// Live v1.0.3: AUHosting and punch2penEngine are ESTABLISHED on 7483 and the
// editor stays on WAIT. An open socket is not HandshakeResponse. "GET "
// (wire type 542393671) is not a live client. A second bind must fail while
// the first listener still holds 7483 and can still answer Handshake.
bool pluginWouldLeaveWait(bool sawHandshakeResponse, uint32_t accepted) {
  return sawHandshakeResponse && accepted == 1;
}

// 0x20544547, the MessageType an HTTP "GET " prefix decodes as.
constexpr uint32_t kHttpGetWireType = 542393671u;

bool socketClosed(int fd) {
  char probe = 0;
  const ssize_t n = ::recv(fd, &probe, 1, MSG_PEEK | MSG_DONTWAIT);
  if (n == 0)
    return true;
  if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    return true;
  return false;
}

bool socketHasFrame(int fd) {
  char probe = 0;
  return ::recv(fd, &probe, 1, MSG_PEEK | MSG_DONTWAIT) > 0;
}

// Accept, an early non-handshake frame, and a rejected handshake are not a
// live client. sendResult / sendProfileStatus must not hit the socket, and
// nothing may be queued for the coordinator.
void assertClientNotLive(punch2pen::IPCServer &server, int fd, const char *why) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
  while (std::chrono::steady_clock::now() < deadline) {
    server.sendResult("lyric", 0.0, 1.0, 1);
    server.sendProfileStatus(R"({"type":"profileStatus"})");
    if (socketClosed(fd)) {
      std::cerr << why << ": socket closed before HandshakeResponse\n";
      assert(false);
    }
    if (socketHasFrame(fd)) {
      std::cerr << why
                << ": engine wrote a frame before HandshakeResponse; "
                   "TCP accept is not a live client and the plugin stays "
                   "on WAIT\n";
      assert(false);
    }
    if (server.hasPendingAudio() || server.hasPendingCorrection() ||
        server.hasPendingProfileCommand()) {
      std::cerr << why
                << ": engine published the socket before HandshakeResponse\n";
      assert(false);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

// "GET " may reset that one TCP connection. It must not publish the client
// or free 127.0.0.1:7483 for a second engine.
void assertPrefixNotLive(punch2pen::IPCServer &server, int fd, const char *why) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
  while (std::chrono::steady_clock::now() < deadline) {
    server.sendResult("lyric", 0.0, 1.0, 1);
    server.sendProfileStatus(R"({"type":"profileStatus"})");
    if (socketHasFrame(fd)) {
      std::cerr << why
                << ": non-handshake prefix was treated as a live client\n";
      assert(false);
    }
    if (server.hasPendingAudio() || server.hasPendingCorrection() ||
        server.hasPendingProfileCommand()) {
      std::cerr << why
                << ": non-handshake prefix published the client\n";
      assert(false);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void assertPortStillHeld(int port) {
  punch2pen::IPCServer intruder(port);
  if (intruder.start()) {
    intruder.stop();
    std::cerr << "second engine bound 127.0.0.1:" << port
              << " while the first still holds it\n";
    assert(false);
  }
}

bool readHandshakeResponse(int fd, uint32_t &accepted) {
  punch2pen::protocol::Header header{};
  std::vector<char> payload;
  if (!readMessage(fd, header, payload)) {
    std::cerr << "TCP accept without HandshakeResponse leaves the plugin on "
                 "WAIT\n";
    return false;
  }
  if (header.type != punch2pen::protocol::MessageType::HandshakeResponse ||
      payload.size() != sizeof(punch2pen::protocol::HandshakeResponse)) {
    std::cerr << "frame type " << static_cast<uint32_t>(header.type)
              << " arrived before HandshakeResponse\n";
    return false;
  }
  punch2pen::protocol::HandshakeResponse response{};
  std::memcpy(&response, payload.data(), sizeof(response));
  accepted = response.accepted;
  return response.version == punch2pen::protocol::kProtocolVersion;
}

void testHandshakeResponseRequiredBeforeLiveClient() {
  // Production port, one bind. A rejected handshake must not force the
  // engine to listen again.
  constexpr int kPort = 7483;
  punch2pen::IPCServer server(kPort);
  if (!server.start()) {
    std::cerr << "could not bind 127.0.0.1:7483; refusing to skip the "
                 "handshake gate\n";
    assert(false);
  }

  assertPortStillHeld(kPort);

  const int http = connectLocal(kPort);
  assert(http >= 0);
  // Accept happened. That alone must not count as a live client.
  assertClientNotLive(server, http, "after TCP accept");

  const char kHttpGet[] =
      "GET / HTTP/1.1\r\nHost: 127.0.0.1:7483\r\nConnection: close\r\n\r\n";
  uint32_t wireType = 0;
  std::memcpy(&wireType, kHttpGet, sizeof(wireType));
  assert(wireType == kHttpGetWireType);
  if (!sendAll(http, kHttpGet, sizeof(kHttpGet) - 1)) {
    std::cerr << "GET prefix closed the listener\n";
    assert(false);
  }
  assertPrefixNotLive(server, http, "after HTTP GET");
  assertPortStillHeld(kPort);
  ::close(http);

  const int fd = connectLocal(kPort);
  if (fd < 0) {
    std::cerr << "GET required a second bind on 127.0.0.1:7483\n";
    assert(false);
  }

  const std::string early = R"({"op":"status"})";
  punch2pen::protocol::Header earlyHeader{};
  earlyHeader.type = punch2pen::protocol::MessageType::ProfileCommand;
  earlyHeader.length = static_cast<uint32_t>(early.size());
  if (!sendAll(fd, &earlyHeader, sizeof(earlyHeader)) ||
      !sendAll(fd, early.data(), early.size())) {
    std::cerr << "early non-handshake frame closed the socket\n";
    assert(false);
  }
  assertClientNotLive(server, fd, "after early non-handshake frame");

  if (!sendHandshake(fd, 0)) {
    std::cerr << "early non-handshake frame closed the socket\n";
    assert(false);
  }
  uint32_t accepted = 1;
  if (!readHandshakeResponse(fd, accepted) || accepted != 0) {
    std::cerr << "rejected handshake did not answer with HandshakeResponse "
                 "accepted=0\n";
    assert(false);
  }
  assert(!pluginWouldLeaveWait(true, accepted));
  assertClientNotLive(server, fd, "after rejected handshake");

  // Same listener. start() is not called again.
  const int retry = connectLocal(kPort);
  if (retry < 0) {
    std::cerr << "failed handshake required a second bind on 127.0.0.1:7483\n";
    assert(false);
  }

  if (!sendHandshake(fd, punch2pen::protocol::kProtocolVersion)) {
    std::cerr << "socket closed before a completed handshake\n";
    assert(false);
  }
  accepted = 0;
  if (!readHandshakeResponse(fd, accepted) || accepted != 1 ||
      !pluginWouldLeaveWait(true, accepted)) {
    std::cerr << "TCP accept without HandshakeResponse leaves the plugin on "
                 "WAIT\n";
    assert(false);
  }

  server.sendResult("lyric", 1.25, 2.5, 7);
  punch2pen::protocol::Header transcript{};
  std::vector<char> transcriptPayload;
  if (!readMessage(fd, transcript, transcriptPayload) ||
      transcript.type != punch2pen::protocol::MessageType::TranscriptionResult) {
    std::cerr << "transcript frame was not held until after HandshakeResponse\n";
    assert(false);
  }
  punch2pen::protocol::TranscriptionResultHeader result{};
  assert(transcriptPayload.size() >= sizeof(result));
  std::memcpy(&result, transcriptPayload.data(), sizeof(result));
  const std::string text(transcriptPayload.begin() + sizeof(result),
                         transcriptPayload.end());
  assert(result.textLength == 5);
  assert(text == "lyric");

  const std::string live = R"({"op":"status","live":1})";
  punch2pen::protocol::Header liveHeader{};
  liveHeader.type = punch2pen::protocol::MessageType::ProfileCommand;
  liveHeader.length = static_cast<uint32_t>(live.size());
  assert(sendAll(fd, &liveHeader, sizeof(liveHeader)));
  assert(sendAll(fd, live.data(), live.size()));
  bool gotCommand = false;
  for (int i = 0; i < 50 && !gotCommand; ++i) {
    gotCommand = server.hasPendingProfileCommand();
    if (!gotCommand)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(gotCommand);
  assert(server.popProfileCommand() == live);

  assert(sendHandshake(retry, punch2pen::protocol::kProtocolVersion));
  uint32_t retryAccepted = 0;
  assert(readHandshakeResponse(retry, retryAccepted));
  assert(pluginWouldLeaveWait(true, retryAccepted));

  ::close(retry);
  ::close(fd);
  server.stop();
  std::cout << "[PASS] testHandshakeResponseRequiredBeforeLiveClient"
            << std::endl;
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
  testHandshakeResponseRequiredBeforeLiveClient();
  std::cout << "All IPC handshake tests passed!" << std::endl;
  return 0;
}
