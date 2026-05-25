#include "../src/IPCServerInterface.h"
#include "../src/TranscriberInterface.h"
#include "../src/TranscriptionCoordinator.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

class MockIPCServer : public Punch2Pen::IPCServerInterface {
public:
  bool hasPendingAudio() override { return !audioQueue.empty(); }

  std::vector<float> popAudio() override {
    if (audioQueue.empty())
      return {};
    auto block = audioQueue.front();
    audioQueue.erase(audioQueue.begin());
    return block;
  }

  bool transportStateChangedToStop() override {
    bool status = simulatedStopTriggered;
    simulatedStopTriggered = false;
    return status;
  }

  std::vector<std::vector<float>> audioQueue;
  bool simulatedStopTriggered = false;
};

class MockTranscriber : public Punch2Pen::TranscriberInterface {
public:
  void addListener(Listener *l) override { listener = l; }
  void removeListener(Listener *l) override {
    if (listener == l)
      listener = nullptr;
  }

  void pushAudioBlock(const float *samples, int sampleCount,
                      double dawSampleTime) override {
    (void)samples;
    (void)dawSampleTime;
    pushAudioBlockCalled = true;
    lastSampleCountReceived = sampleCount;
  }

  void setVocabularyBias(const std::vector<std::string> &words) override {
    setVocabularyBiasCalled = true;
    lastVocabularyReceived = words;
  }

  void finalizeStream() override { finalizeStreamCalled = true; }

  Listener *listener = nullptr;
  bool pushAudioBlockCalled = false;
  bool setVocabularyBiasCalled = false;
  bool finalizeStreamCalled = false;
  int lastSampleCountReceived = 0;
  std::vector<std::string> lastVocabularyReceived;
};

void testCoordinatorRouting() {
  MockIPCServer mockServer;
  MockTranscriber mockTranscriber;

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber);

  std::vector<float> fakeDAWAudio(1600, 0.5f);
  mockServer.audioQueue.push_back(fakeDAWAudio);
  mockServer.simulatedStopTriggered = true;

  std::thread worker([&]() { coordinator.run(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  coordinator.stop();
  worker.join();

  assert(mockTranscriber.pushAudioBlockCalled &&
         "Error: Coordinator failed to route audio blocks to the Transcriber interface!");
  assert(mockTranscriber.lastSampleCountReceived == 1600 &&
         "Error: Audio packet sample count got corrupted in routing loop!");
  assert(mockTranscriber.finalizeStreamCalled &&
         "Error: Coordinator missed the DAW transport stop trigger event!");

  std::cout << "✅ [Principal Review]: TranscriptionCoordinator Architecture Unit Test PASSED Successfully!"
            << std::endl;
}

int main() {
  testCoordinatorRouting();
  return 0;
}
