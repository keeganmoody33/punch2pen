#include "../src/DatabaseManager.h"
#include "../src/IPCServerInterface.h"
#include "../src/TranscriberInterface.h"
#include "../src/TranscriptionCoordinator.h"
#include <cassert>
#include <chrono>
#include <filesystem>
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
    lastDawSampleTime_ = nextDawSampleTime;
    return block;
  }

  double lastAudioDawSampleTime() override { return lastDawSampleTime_; }

  bool transportStateChangedToStop() override {
    bool status = simulatedStopTriggered;
    simulatedStopTriggered = false;
    return status;
  }

  bool hasPendingCorrection() override { return !correctionQueue.empty(); }

  CorrectionPair popCorrection() override {
    if (correctionQueue.empty())
      return {};
    auto c = correctionQueue.front();
    correctionQueue.erase(correctionQueue.begin());
    return c;
  }

  std::vector<std::vector<float>> audioQueue;
  bool simulatedStopTriggered = false;
  std::vector<CorrectionPair> correctionQueue;
  double nextDawSampleTime = 0.0;
  double lastDawSampleTime_ = 0.0;
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
    pushAudioBlockCalled = true;
    lastSampleCountReceived = sampleCount;
    lastDawSampleTime = dawSampleTime;
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
  double lastDawSampleTime = 0.0;
  std::vector<std::string> lastVocabularyReceived;
};

void testCoordinatorRouting() {
  MockIPCServer mockServer;
  MockTranscriber mockTranscriber;

  std::string tmpDir = "/tmp/punch2pen_test_coord";
  std::filesystem::create_directories(tmpDir);
  punch2pen::DatabaseManager db;
  db.initialize(tmpDir + "/test_corrections.csv");

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber, db);

  std::vector<float> fakeDAWAudio(1600, 0.5f);
  mockServer.audioQueue.push_back(fakeDAWAudio);
  mockServer.simulatedStopTriggered = true;
  mockServer.nextDawSampleTime = 48000.0;

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
  assert(mockTranscriber.lastDawSampleTime == 48000.0 &&
         "Error: DAW sample time not forwarded correctly!");

  std::cout << "[PASS] testCoordinatorRouting" << std::endl;

  std::filesystem::remove_all(tmpDir);
}

void testCoordinatorCorrections() {
  MockIPCServer mockServer;
  MockTranscriber mockTranscriber;

  std::string tmpDir = "/tmp/punch2pen_test_coord_corr";
  std::filesystem::create_directories(tmpDir);
  punch2pen::DatabaseManager db;
  db.initialize(tmpDir + "/test_corrections.csv");

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber, db);

  mockServer.correctionQueue.push_back({"hello", "world"});

  std::thread worker([&]() { coordinator.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  coordinator.stop();
  worker.join();

  assert(mockTranscriber.setVocabularyBiasCalled &&
         "Error: Coordinator did not update vocabulary after correction!");

  auto vocab = db.getVocabulary();
  assert(!vocab.empty() && "Error: DatabaseManager vocabulary empty after correction!");

  std::cout << "[PASS] testCoordinatorCorrections" << std::endl;

  std::filesystem::remove_all(tmpDir);
}

int main() {
  testCoordinatorRouting();
  testCoordinatorCorrections();
  std::cout << "All TranscriptionCoordinator tests passed!" << std::endl;
  return 0;
}
