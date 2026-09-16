#pragma once

#include "../../shared/Protocol.h"
#include <JuceHeader.h>
#include <cstdint>

namespace punch2pen {

class IPCClient : public juce::Thread {
public:
  enum class TranscriptionMode { Offline, Online };

  explicit IPCClient(int port = 7483, bool autoLaunchEngine = true);
  ~IPCClient() override;

  void run() override;

  bool isConnected() const;
  void sendAudioChunk(const float *samples, int numSamples, double sampleRate,
                      double dawSampleTime, uint32_t captureEpoch = 0);
  void sendTransportStop(uint32_t captureEpoch = 0);
  void flagTransportStop(uint32_t epoch = 0);
  void sendCorrection(const std::string &original, const std::string &corrected);
  void setTranscriptionMode(TranscriptionMode mode);
  TranscriptionMode getTranscriptionMode() const;

  // Callback interface for receiving messages
  struct Listener {
    virtual ~Listener() = default;
    virtual void onTranscriptionReceived(const std::string &text,
                                         double startTime, double endTime,
                                         uint32_t captureEpoch) = 0;
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
  bool popStopEpoch(uint32_t &epoch);
  void processOutgoingAudio(bool flushPartial = false,
                            uint32_t stopEpoch = 0);

  juce::StreamingSocket socket;
  std::atomic<bool> connected{false};
  bool shouldStop = false;

  class AudioRingBuffer *ringBuffer = nullptr;
  std::vector<float> tempBuffer;
  std::atomic<TranscriptionMode> transcriptionMode{TranscriptionMode::Offline};
  std::atomic<double> hostSampleRate{0.0};
  std::atomic<bool> pendingCaptureReset{false};
  juce::AbstractFifo stopFifo{64};
  std::vector<uint32_t> stopEpochs;

  int serverPort;
  bool autoLaunchEngine;
  juce::CriticalSection listenerLock;
  std::vector<Listener *> listeners;
};

} // namespace punch2pen
