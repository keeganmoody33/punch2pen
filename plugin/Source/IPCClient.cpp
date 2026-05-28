#include "IPCClient.h"
#include "RingBuffer.h"

#include <algorithm>

#if JUCE_MAC || JUCE_IOS
#include <sys/socket.h>
#endif

namespace punch2pen {

IPCClient::IPCClient(int port, bool autoLaunchEngineFlag)
    : Thread("Punch2Pen_IPC"), serverPort(port),
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
                l->onTranscriptionReceived(text);
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

    processOutgoingAudio();

    if (pendingStop.exchange(false))
      sendTransportStop();
  }
}

void IPCClient::processOutgoingAudio() {
  if (!connected || !ringBuffer)
    return;

  int available = ringBuffer->getNumReady();
  if (available > 0) {
    const int chunkSize = transcriptionMode.load() == TranscriptionMode::Online
                              ? 1600
                              : 4096;
    if (available < chunkSize)
      return;

    if (tempBuffer.size() < (size_t)chunkSize)
      tempBuffer.resize((size_t)chunkSize);

    ringBuffer->read(tempBuffer.data(), chunkSize);
    // Using standard 48k for now, realistically should get from processor
    sendAudioChunk(tempBuffer.data(), chunkSize, 48000.0);
  }
}

void IPCClient::attemptConnection() {
  // Localhost, configurable port (default 7483 from spec)
  if (socket.connect("127.0.0.1", serverPort, 1000)) {
#if JUCE_MAC || JUCE_IOS
    int disableSigPipe = 1;
    setsockopt(socket.getRawSocketHandle(), SOL_SOCKET, SO_NOSIGPIPE,
               &disableSigPipe, sizeof(disableSigPipe));
#endif
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

void IPCClient::launchEngine() {
  // Try default install location
  juce::File engineApp("/Applications/Punch2Pen/punch2penEngine");

  if (!engineApp.existsAsFile()) {
    // For development, try finding it relative to home
    // This is a bit of a hack for testing on dev machine
    juce::File home =
        juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    engineApp = home.getChildFile(
        "punch2pen/bin/punch2penEngine"); // Assuming symlink or copy

    // Or check build dir if known... too varying.
    if (!engineApp.existsAsFile()) {
      engineApp = home.getChildFile("punch2pen/build/bin/punch2penEngine");
    }
  }

  if (engineApp.existsAsFile()) {
    // Launch in background
    juce::String command =
        "nohup \"" + engineApp.getFullPathName() + "\" > /dev/null 2>&1 &";
    system(command.toRawUTF8());
  }
}

bool IPCClient::isConnected() const { return connected; }

void IPCClient::sendAudioChunk(const float *samples, int numSamples,
                               double sampleRate) {
  if (!connected)
    return;

  protocol::Header header;
  header.type = protocol::MessageType::AudioChunk;

  protocol::AudioChunkHeader chunkHeader;
  chunkHeader.sampleRate = sampleRate;
  chunkHeader.numSamples = (uint32_t)numSamples;
  chunkHeader.dawSampleTime = 0.0;

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

void IPCClient::sendTransportStop() {
  if (!connected)
    return;

  protocol::Header header;
  header.type = protocol::MessageType::TransportStop;
  header.length = 0;

  if (socket.write(&header, sizeof(header)) != sizeof(header)) {
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
