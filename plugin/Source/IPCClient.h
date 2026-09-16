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
  void sendAudioChunk(const float *samples, int numSamples, double sampleRate,
                      double dawSampleTime);
  void sendTransportStop();
  void flagTransportStop();
  void sendCorrection(const std::string &original, const std::string &corrected);
  void setTranscriptionMode(TranscriptionMode mode);
  TranscriptionMode getTranscriptionMode() const;

  // Callback interface for receiving messages
  struct Listener {
    virtual ~Listener() = default;
    virtual void onTranscriptionReceived(const std::string &text,
                                         double startTime,
                                         double endTime) = 0;
    virtual void onStatusChanged(bool isConnected) = 0;
  };

  void addListener(Listener *listener);
  void removeListener(Listener *listener);

  void setAudioSource(class AudioRingBuffer *buffer) { ringBuffer = buffer; }
  void setHostSampleRate(double sampleRate);
  // Consumer-thread FIFO reset. Safe to call from the audio thread; the
  // IPC thread applies it before the next read. Wakes a reconnect wait.
  void requestCaptureReset();
  bool captureResetPending() const { return pendingCaptureReset.load(); }

private:
  void attemptConnection();
  void launchEngine();
  void handleMessage();
  void applyPendingCaptureReset();
  void processOutgoingAudio(bool flushPartial = false);

  juce::StreamingSocket socket;
  std::atomic<bool> connected{false};
  bool shouldStop = false;

  class AudioRingBuffer *ringBuffer = nullptr;
  std::vector<float> tempBuffer;
  std::atomic<TranscriptionMode> transcriptionMode{TranscriptionMode::Offline};
  std::atomic<double> hostSampleRate{0.0};
  std::atomic<bool> pendingCaptureReset{false};

  int serverPort;
  bool autoLaunchEngine;
  juce::CriticalSection listenerLock;
  std::vector<Listener *> listeners;

  std::atomic<bool> pendingStop{false};
};

} // namespace punch2pen
