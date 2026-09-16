#include "IPCClient.h"
#include "RingBuffer.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>

#if JUCE_MAC || JUCE_IOS
#include <fcntl.h>
#include <spawn.h>
#include <sys/socket.h>
#include <unistd.h>
extern char **environ;
#endif

namespace punch2pen {

namespace {
std::once_flag g_engineLaunchOnce;

bool writeExact(juce::StreamingSocket &socket, const void *data, int len) {
  const auto *p = static_cast<const char *>(data);
  int sent = 0;
  while (sent < len) {
    const int n = socket.write(p + sent, len - sent);
    if (n <= 0)
      return false;
    sent += n;
  }
  return true;
}

bool readExact(juce::StreamingSocket &socket, void *data, int len) {
  auto *p = static_cast<char *>(data);
  int got = 0;
  while (got < len) {
    const int n = socket.read(p + got, len - got, true);
    if (n <= 0)
      return false;
    got += n;
  }
  return true;
}
} // namespace

IPCClient::IPCClient(int port, bool autoLaunchEngineFlag)
    : Thread("Punch2Pen_IPC"), stopEpochs(64, 0), serverPort(port),
      autoLaunchEngine(autoLaunchEngineFlag) {
  startThread();
}

IPCClient::~IPCClient() {
  signalThreadShouldExit();
  socket.close();
  stopThread(1000);
}

void IPCClient::run() {
  tempBuffer.reserve(4096);
  while (!threadShouldExit()) {
    applyPendingCaptureReset();
    if (!connected) {
      attemptConnection();
      if (!connected) {
        wait(2000);
        continue;
      }
    }

    // Read loop (non-blocking if possible or short timeout)
    if (socket.waitUntilReady(true, 10)) {
      protocol::Header header;
      int bytesRead = socket.read(&header, sizeof(header), false);

      if (bytesRead == sizeof(header)) {
        if (header.type == protocol::MessageType::TranscriptionResult) {
          protocol::TranscriptionResultHeader resultHeader;
          if (socket.read(&resultHeader, sizeof(resultHeader), false) ==
              sizeof(resultHeader)) {
            juce::MemoryBlock textData(resultHeader.textLength + 1);
            if (socket.read(textData.getData(), (int)resultHeader.textLength,
                            false) == (int)resultHeader.textLength) {
              ((char *)textData.getData())[resultHeader.textLength] = 0;
              std::string text((char *)textData.getData());

              juce::ScopedLock lock(listenerLock);
              for (auto *l : listeners)
                l->onTranscriptionReceived(text, resultHeader.startTime,
                                           resultHeader.endTime,
                                           resultHeader.captureEpoch);
            }
          }
        } else {
          if (header.length > 0) {
            juce::MemoryBlock skip(header.length);
            socket.read(skip.getData(), (int)header.length, false);
          }
        }
      } else if (bytesRead < 0) {
        connected = false;
        juce::ScopedLock lock(listenerLock);
        for (auto *l : listeners)
          l->onStatusChanged(false);
      }
    }

    // Drain every queued punch-out: matching epoch then one TransportStop.
    uint32_t stopEpoch = 0;
    bool drainedStop = false;
    while (popStopEpoch(stopEpoch)) {
      processOutgoingAudio(true, stopEpoch);
      sendTransportStop(stopEpoch);
      drainedStop = true;
    }
    if (!drainedStop)
      processOutgoingAudio(false);
  }
}

void IPCClient::processOutgoingAudio(bool flushPartial, uint32_t stopEpoch) {
  if (!connected || !ringBuffer)
    return;

  const double sampleRate = hostSampleRate.load();
  if (sampleRate <= 0.0)
    return;

  const int chunkSize = transcriptionMode.load() == TranscriptionMode::Online
                            ? 1600
                            : 4096;

  while (true) {
    const int available = ringBuffer->getNumReady();
    if (available <= 0)
      return;

    if (flushPartial && ringBuffer->peekEpoch() != stopEpoch)
      return;

    int toRead = chunkSize;
    if (available < chunkSize) {
      if (!flushPartial)
        return;
      toRead = available;
    }

    if (tempBuffer.size() < (size_t)toRead)
      tempBuffer.resize((size_t)toRead);

    double chunkDawSample = 0.0;
    uint32_t epoch = 0;
    const int n = ringBuffer->read(tempBuffer.data(), toRead, &chunkDawSample,
                                   &epoch);
    if (n <= 0)
      return;
    sendAudioChunk(tempBuffer.data(), n, sampleRate, chunkDawSample, epoch);
    if (!flushPartial)
      return;
  }
}

void IPCClient::applyPendingCaptureReset() {
  if (!pendingCaptureReset.load())
    return;
  // Consumer-only. Punch-out drains by epoch instead of reset(), so a
  // concurrent punch-in is not discarded.
  if (ringBuffer)
    ringBuffer->reset();
  pendingCaptureReset.store(false);
}

void IPCClient::attemptConnection() {
  // Localhost, configurable port (default 7483 from spec)
  if (socket.connect("127.0.0.1", serverPort, 1000)) {
#if JUCE_MAC || JUCE_IOS
    int disableSigPipe = 1;
    setsockopt(socket.getRawSocketHandle(), SOL_SOCKET, SO_NOSIGPIPE,
               &disableSigPipe, sizeof(disableSigPipe));
#endif
    if (!completeHandshake()) {
      socket.close();
      connected = false;
      if (autoLaunchEngine)
        launchEngine();
      return;
    }
    connected = true;

    juce::ScopedLock lock(listenerLock);
    for (auto *l : listeners) {
      l->onStatusChanged(true);
    }
  } else {
    if (autoLaunchEngine)
      launchEngine();
  }
}

bool IPCClient::completeHandshake() {
  protocol::Header header{};
  header.type = protocol::MessageType::Handshake;
  protocol::Handshake handshake{};
  handshake.version = protocol::kProtocolVersion;
  header.length = (uint32_t)sizeof(handshake);

  if (!writeExact(socket, &header, sizeof(header)))
    return false;
  if (!writeExact(socket, &handshake, sizeof(handshake)))
    return false;

  if (!socket.waitUntilReady(true, 1000))
    return false;

  protocol::Header reply{};
  if (!readExact(socket, &reply, sizeof(reply)))
    return false;
  if (reply.type != protocol::MessageType::HandshakeResponse ||
      reply.length != (uint32_t)sizeof(protocol::HandshakeResponse))
    return false;

  protocol::HandshakeResponse response{};
  if (!readExact(socket, &response, sizeof(response)))
    return false;

  return response.accepted != 0 &&
         response.version == protocol::kProtocolVersion;
}

juce::File IPCClient::resolveEngineBinary() const {
  if (const char *overridePath = std::getenv("PUNCH2PEN_ENGINE")) {
    juce::File fromEnv(overridePath);
    if (fromEnv.existsAsFile())
      return fromEnv;
  }

  const juce::File home =
      juce::File::getSpecialLocation(juce::File::userHomeDirectory);
  // Prefer the user-writable helper so a local test install is not shadowed
  // by an older system-wide engine from the pkg.
  juce::File engineApp = home.getChildFile("punch2pen/bin/punch2penEngine");
  if (engineApp.existsAsFile())
    return engineApp;

  engineApp = juce::File("/Applications/Punch2Pen/punch2penEngine");
  if (engineApp.existsAsFile())
    return engineApp;

  return {};
}

void IPCClient::launchEngine() {
  std::call_once(g_engineLaunchOnce, [this]() {
    const juce::File engineApp = resolveEngineBinary();
    if (!engineApp.existsAsFile())
      return;

#if JUCE_MAC
    const juce::File dataDir =
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile(".punch2pen");
    dataDir.createDirectory();
    const juce::File logFile = dataDir.getChildFile("engine.log");
    const juce::String enginePath = engineApp.getFullPathName();
    const juce::String logPath = logFile.getFullPathName();

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO,
                                     logPath.toRawUTF8(),
                                     O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

    const char *argv[] = {enginePath.toRawUTF8(), nullptr};
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, enginePath.toRawUTF8(), &actions, &attr,
                               const_cast<char **>(argv), environ);

    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0)
      return;
#endif
  });
}

bool IPCClient::isConnected() const { return connected; }

void IPCClient::setHostSampleRate(double sampleRate) {
  if (sampleRate > 0.0)
    hostSampleRate.store(sampleRate);
}

void IPCClient::flagTransportStop(uint32_t epoch) {
  int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
  stopFifo.prepareToWrite(1, start1, size1, start2, size2);
  if (size1 > 0)
    stopEpochs[static_cast<size_t>(start1)] = epoch;
  else if (size2 > 0)
    stopEpochs[static_cast<size_t>(start2)] = epoch;
  else
    return;
  stopFifo.finishedWrite(1);
  notify();
}

bool IPCClient::popStopEpoch(uint32_t &epoch) {
  int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
  stopFifo.prepareToRead(1, start1, size1, start2, size2);
  if (size1 > 0)
    epoch = stopEpochs[static_cast<size_t>(start1)];
  else if (size2 > 0)
    epoch = stopEpochs[static_cast<size_t>(start2)];
  else
    return false;
  stopFifo.finishedRead(1);
  return true;
}

void IPCClient::requestCaptureReset() {
  pendingCaptureReset.store(true);
  notify();
}

void IPCClient::sendAudioChunk(const float *samples, int numSamples,
                               double sampleRate, double dawSampleTime,
                               uint32_t captureEpoch) {
  if (!connected)
    return;

  protocol::Header header;
  header.type = protocol::MessageType::AudioChunk;

  protocol::AudioChunkHeader chunkHeader;
  chunkHeader.sampleRate = sampleRate;
  chunkHeader.numSamples = (uint32_t)numSamples;
  chunkHeader.dawSampleTime = dawSampleTime;
  chunkHeader.captureEpoch = captureEpoch;

  size_t payloadSize =
      sizeof(protocol::AudioChunkHeader) + ((size_t)numSamples * sizeof(float));
  header.length = (uint32_t)payloadSize;

  if (socket.write(&header, sizeof(header)) != sizeof(header)) {
    connected = false;
    return;
  }
  if (socket.write(&chunkHeader, sizeof(chunkHeader)) != sizeof(chunkHeader)) {
    connected = false;
    return;
  }
  if (socket.write(samples, (int)((size_t)numSamples * sizeof(float))) !=
      (int)((size_t)numSamples * sizeof(float))) {
    connected = false;
    return;
  }
}

void IPCClient::sendTransportStop(uint32_t captureEpoch) {
  if (!connected)
    return;

  protocol::Header header;
  header.type = protocol::MessageType::TransportStop;
  protocol::TransportStopHeader stopHeader;
  stopHeader.captureEpoch = captureEpoch;
  header.length = (uint32_t)sizeof(stopHeader);

  if (socket.write(&header, sizeof(header)) != sizeof(header)) {
    connected = false;
    return;
  }
  if (socket.write(&stopHeader, sizeof(stopHeader)) != sizeof(stopHeader)) {
    connected = false;
  }
}

void IPCClient::sendCorrection(const std::string &original,
                               const std::string &corrected) {
  if (!connected)
    return;

  protocol::Header header;
  header.type = protocol::MessageType::Correction;

  protocol::CorrectionHeader corrHeader;
  corrHeader.originalLength = (uint32_t)original.size();
  corrHeader.correctedLength = (uint32_t)corrected.size();

  header.length = (uint32_t)(sizeof(corrHeader) + original.size() + corrected.size());

  if (socket.write(&header, sizeof(header)) != sizeof(header)) {
    connected = false;
    return;
  }
  if (socket.write(&corrHeader, sizeof(corrHeader)) != sizeof(corrHeader)) {
    connected = false;
    return;
  }
  if (socket.write(original.data(), (int)original.size()) != (int)original.size()) {
    connected = false;
    return;
  }
  if (socket.write(corrected.data(), (int)corrected.size()) != (int)corrected.size()) {
    connected = false;
    return;
  }
}

void IPCClient::setTranscriptionMode(TranscriptionMode mode) {
  transcriptionMode.store(mode);
}

IPCClient::TranscriptionMode IPCClient::getTranscriptionMode() const {
  return transcriptionMode.load();
}

void IPCClient::addListener(Listener *listener) {
  juce::ScopedLock lock(listenerLock);
  if (std::find(listeners.begin(), listeners.end(), listener) ==
      listeners.end()) {
    listeners.push_back(listener);
  }
}

void IPCClient::removeListener(Listener *listener) {
  juce::ScopedLock lock(listenerLock);
  listeners.erase(std::remove(listeners.begin(), listeners.end(), listener),
                  listeners.end());
}

} // namespace punch2pen
