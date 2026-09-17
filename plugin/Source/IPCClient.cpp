#include "IPCClient.h"
#include "RingBuffer.h"
#include "UserHome.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#if JUCE_MAC || JUCE_IOS
#include <fcntl.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/socket.h>
#include <unistd.h>
extern char **environ;
#endif

namespace punch2pen {

namespace {
constexpr uint32_t kLaunchCooldownMs = 4000;

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

  if (!socket.waitUntilReady(true, 2000))
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

namespace {

void pluginIpcLog(const juce::String &line) {
  const juce::File dataDir =
      juce::File(juce::String(punch2pen::punch2penDataDir()));
  dataDir.createDirectory();
  dataDir.getChildFile("plugin-ipc.log")
      .appendText(juce::Time::getCurrentTime().toString(true, true) + " " +
                  line + "\n");
}

juce::File pluginContentsDir() {
  return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
      .getParentDirectory()
      .getParentDirectory();
}

void considerApp(std::vector<juce::File> &apps, const juce::File &app) {
  if (!(app.isDirectory() && app.hasFileExtension("app")))
    return;
  for (const auto &existing : apps) {
    if (existing == app)
      return;
  }
  apps.push_back(app);
}

void considerBinary(std::vector<juce::File> &bins, const juce::File &bin) {
  if (!bin.existsAsFile())
    return;
  for (const auto &existing : bins) {
    if (existing == bin)
      return;
  }
  bins.push_back(bin);
}

#if JUCE_MAC
bool openEngineApp(const juce::File &app) {
  const std::string path = app.getFullPathName().toStdString();
  const char *argv[] = {"/usr/bin/open", "-g", path.c_str(), nullptr};
  pid_t pid = 0;
  const int rc = posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr,
                             const_cast<char **>(argv), environ);
  pluginIpcLog("open -g " + app.getFullPathName() + " rc=" + juce::String(rc));
  return rc == 0;
}

bool spawnBareEngine(const juce::File &engineBin) {
  const std::string home = punch2pen::realUserHome();
  const juce::File dataDir =
      juce::File(juce::String(home)).getChildFile(".punch2pen");
  dataDir.createDirectory();
  const juce::File logFile = dataDir.getChildFile("engine.log");

  const std::string enginePath = engineBin.getFullPathName().toStdString();
  const std::string logPath = logFile.getFullPathName().toStdString();
  std::vector<std::string> envStore = {
      "HOME=" + home,
      "PUNCH2PEN_HOME=" + home,
      "PATH=/usr/bin:/bin:/usr/sbin:/sbin",
      "TMPDIR=/tmp",
  };
  if (const passwd *pw = getpwuid(getuid())) {
    if (pw->pw_name != nullptr && pw->pw_name[0] != '\0')
      envStore.push_back(std::string("USER=") + pw->pw_name);
  }
  std::vector<char *> envp;
  envp.reserve(envStore.size() + 1);
  for (auto &entry : envStore)
    envp.push_back(entry.data());
  envp.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attr;
  posix_spawn_file_actions_init(&actions);
  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&attr, 0);
  const int openRc = posix_spawn_file_actions_addopen(
      &actions, STDOUT_FILENO, logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND,
      0644);
  if (openRc == 0)
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

  const char *argv[] = {enginePath.c_str(), nullptr};
  pid_t pid = 0;
  const int rc =
      posix_spawn(&pid, enginePath.c_str(), openRc == 0 ? &actions : nullptr,
                  &attr, const_cast<char **>(argv), envp.data());

  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&actions);
  pluginIpcLog("posix_spawn " + engineBin.getFullPathName() +
               " rc=" + juce::String(rc));
  return rc == 0;
}
#endif

} // namespace

juce::File IPCClient::resolveEngineApp() const {
  if (const char *overridePath = std::getenv("PUNCH2PEN_ENGINE")) {
    juce::File fromEnv(overridePath);
    if (fromEnv.isDirectory() && fromEnv.hasFileExtension("app"))
      return fromEnv;
  }

  const juce::File nested =
      pluginContentsDir().getChildFile("Helpers/punch2penEngine.app");
  if (nested.isDirectory())
    return nested;

  const juce::File systemApp("/Applications/Punch2Pen/punch2penEngine.app");
  if (systemApp.isDirectory())
    return systemApp;

  return {};
}

juce::File IPCClient::resolveEngineBinary() const {
  if (const char *overridePath = std::getenv("PUNCH2PEN_ENGINE")) {
    juce::File fromEnv(overridePath);
    if (fromEnv.existsAsFile())
      return fromEnv;
    if (fromEnv.isDirectory() && fromEnv.hasFileExtension("app")) {
      const juce::File inner =
          fromEnv.getChildFile("Contents/MacOS/punch2penEngine");
      if (inner.existsAsFile())
        return inner;
    }
  }

  const juce::File nested =
      pluginContentsDir().getChildFile("Helpers/punch2penEngine.app");
  if (nested.isDirectory()) {
    const juce::File inner =
        nested.getChildFile("Contents/MacOS/punch2penEngine");
    if (inner.existsAsFile())
      return inner;
  }

  const juce::File systemApp("/Applications/Punch2Pen/punch2penEngine.app");
  if (systemApp.isDirectory()) {
    const juce::File inner =
        systemApp.getChildFile("Contents/MacOS/punch2penEngine");
    if (inner.existsAsFile())
      return inner;
  }

  const juce::File systemBin("/Applications/Punch2Pen/punch2penEngine");
  if (systemBin.existsAsFile())
    return systemBin;

  return {};
}

void IPCClient::launchEngine() {
  const uint32_t now = juce::Time::getMillisecondCounter();
  const uint32_t prev = lastLaunchAttemptMs.load();
  if (prev != 0 && (now - prev) < kLaunchCooldownMs)
    return;
  lastLaunchAttemptMs.store(now);

#if JUCE_MAC
  std::vector<juce::File> apps;
  std::vector<juce::File> bins;

  if (const char *overridePath = std::getenv("PUNCH2PEN_ENGINE")) {
    const juce::File fromEnv(overridePath);
    considerApp(apps, fromEnv);
    considerBinary(bins, fromEnv);
    if (fromEnv.isDirectory() && fromEnv.hasFileExtension("app"))
      considerBinary(bins,
                     fromEnv.getChildFile("Contents/MacOS/punch2penEngine"));
  }

  considerApp(apps,
              pluginContentsDir().getChildFile("Helpers/punch2penEngine.app"));
  considerApp(apps, juce::File("/Applications/Punch2Pen/punch2penEngine.app"));
  considerBinary(bins, juce::File("/Applications/Punch2Pen/punch2penEngine"));

  const juce::File leftoverHome =
      juce::File(juce::String(realUserHome()))
          .getChildFile("punch2pen/bin/punch2penEngine");
  if (leftoverHome.existsAsFile())
    pluginIpcLog("ignoring leftover " + leftoverHome.getFullPathName() +
                 "; packaged engine is preferred");

  pluginIpcLog("launchEngine apps=" + juce::String((int)apps.size()) +
               " bins=" + juce::String((int)bins.size()));

  for (const auto &app : apps) {
    if (openEngineApp(app))
      return;
  }
  for (const auto &bin : bins) {
    if (spawnBareEngine(bin))
      return;
  }
  pluginIpcLog("launchEngine: no engine helper launched");
#endif
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
