#pragma once

#include "../../shared/Protocol.h"
#include <JuceHeader.h>

namespace punch2pen {

class IPCClient : public juce::Thread {
public:
  enum class TranscriptionMode { Offline, Online };

  explicit IPCClient(int port = 7483, bool autoLaunchEngine = true);
  ~IPCClient() override;

  void run() override;

  bool isConnected() const;
  void sendAudioChunk(const float *samples, int numSamples, double sampleRate);
  void sendTransportStop();
  void flagTransportStop() { pendingStop.store(true); }
  void sendCorrection(const std::string &original, const std::string &corrected);
  void setTranscriptionMode(TranscriptionMode mode);
  TranscriptionMode getTranscriptionMode() const;

  // Callback interface for receiving messages
  struct Listener {
    virtual ~Listener() = default;
    virtual void onTranscriptionReceived(const std::string &text) = 0;
    virtual void onStatusChanged(bool isConnected) = 0;
  };

  void addListener(Listener *listener);
  void removeListener(Listener *listener);

  void setAudioSource(class AudioRingBuffer *buffer) { ringBuffer = buffer; }

private:
  void attemptConnection();
  void launchEngine();
  void handleMessage();
  void processOutgoingAudio();

  juce::StreamingSocket socket;
  std::atomic<bool> connected{false};
  bool shouldStop = false;

  class AudioRingBuffer *ringBuffer = nullptr;
  std::vector<float> tempBuffer;
  std::atomic<TranscriptionMode> transcriptionMode{TranscriptionMode::Offline};

  int serverPort;
  bool autoLaunchEngine;
  juce::CriticalSection listenerLock;
  std::vector<Listener *> listeners;

  std::atomic<bool> pendingStop{false};
};

} // namespace punch2pen
