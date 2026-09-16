#include "../src/DatabaseManager.h"
#include "../src/IPCServerInterface.h"
#include "../src/ProfileManager.h"
#include "../src/TranscriberInterface.h"
#include "../src/TranscriptionCoordinator.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

class MockIPCServer : public Punch2Pen::IPCServerInterface {
public:
  struct QueuedEvent {
    bool isStop = false;
    std::vector<float> samples;
    double dawSampleTime = 0.0;
    double sampleRate = 0.0;
    uint32_t captureEpoch = 0;
  };

  bool hasPendingAudio() override {
    return !eventQueue.empty() && !eventQueue.front().isStop;
  }

  std::vector<float> popAudio() override {
    if (eventQueue.empty() || eventQueue.front().isStop)
      return {};
    auto packet = std::move(eventQueue.front());
    eventQueue.erase(eventQueue.begin());
    lastDawSampleTime_ = packet.dawSampleTime;
    lastSampleRate_ = packet.sampleRate;
    lastCaptureEpoch_ = packet.captureEpoch;
    return std::move(packet.samples);
  }

  double lastAudioDawSampleTime() override { return lastDawSampleTime_; }

  double lastAudioSampleRate() override { return lastSampleRate_; }

  uint32_t lastAudioCaptureEpoch() override { return lastCaptureEpoch_; }

  bool transportStateChangedToStop() override {
    if (eventQueue.empty() || !eventQueue.front().isStop)
      return false;
    lastCaptureEpoch_ = eventQueue.front().captureEpoch;
    eventQueue.erase(eventQueue.begin());
    return true;
  }

  bool hasPendingCorrection() override { return !correctionQueue.empty(); }

  CorrectionPair popCorrection() override {
    if (correctionQueue.empty())
      return {};
    auto c = correctionQueue.front();
    correctionQueue.erase(correctionQueue.begin());
    return c;
  }

  void queueAudio(std::vector<float> samples, double dawSampleTime = 0.0,
                  double sampleRate = 0.0, uint32_t captureEpoch = 0) {
    eventQueue.push_back(
        {false, std::move(samples), dawSampleTime, sampleRate, captureEpoch});
  }

  void queueStop(uint32_t captureEpoch = 0) {
    eventQueue.push_back({true, {}, 0.0, 0.0, captureEpoch});
  }

  std::vector<QueuedEvent> eventQueue;
  std::vector<CorrectionPair> correctionQueue;
  double lastDawSampleTime_ = 0.0;
  double lastSampleRate_ = 0.0;
  uint32_t lastCaptureEpoch_ = 0;
};

class MockTranscriber : public Punch2Pen::TranscriberInterface {
public:
  void addListener(Listener *l) override { listener = l; }
  void removeListener(Listener *l) override {
    if (listener == l)
      listener = nullptr;
  }

  void pushAudioBlock(const float *samples, int sampleCount,
                      double dawSampleTime, uint32_t captureEpoch) override {
    (void)samples;
    pushAudioBlockCalled = true;
    lastSampleCountReceived = sampleCount;
    lastDawSampleTime = dawSampleTime;
    lastCaptureEpoch = captureEpoch;
    receivedSampleCounts.push_back(sampleCount);
    callOrder.push_back("audio");
  }

  void setVocabularyBias(const std::vector<std::string> &words) override {
    setVocabularyBiasCalled = true;
    lastVocabularyReceived = words;
    audioCountAtBias = static_cast<int>(receivedSampleCounts.size());
    callOrder.push_back("correction");
  }

  void setInputSampleRate(double sampleRate) override {
    lastInputSampleRate = sampleRate;
  }

  void finalizeStream() override {
    finalizeStreamCalled = true;
    callOrder.push_back("stop");
  }

  Listener *listener = nullptr;
  bool pushAudioBlockCalled = false;
  bool setVocabularyBiasCalled = false;
  bool finalizeStreamCalled = false;
  int lastSampleCountReceived = 0;
  double lastDawSampleTime = 0.0;
  uint32_t lastCaptureEpoch = 0;
  double lastInputSampleRate = 0.0;
  int audioCountAtBias = 0;
  std::vector<std::string> lastVocabularyReceived;
  std::vector<int> receivedSampleCounts;
  std::vector<std::string> callOrder;
};

void testCoordinatorRouting() {
  MockIPCServer mockServer;
  MockTranscriber mockTranscriber;

  std::string tmpDir = "/tmp/punch2pen_test_coord";
  std::filesystem::create_directories(tmpDir);
  punch2pen::DatabaseManager db;
  db.initialize(tmpDir + "/test_corrections.csv");

  punch2pen::ProfileManager profileManager;
  profileManager.setDataDirectory(tmpDir);
  profileManager.loadProfile("test");

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber, db,
                                                  profileManager);

  std::vector<float> fakeDAWAudio(1600, 0.5f);
  mockServer.queueAudio(fakeDAWAudio, 48000.0, 44100.0);
  mockServer.queueStop();

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
  assert(mockTranscriber.lastInputSampleRate == 44100.0 &&
         "Error: Host sample rate not forwarded to TranscriberInterface!");

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

  punch2pen::ProfileManager profileManager;
  profileManager.setDataDirectory(tmpDir);
  profileManager.loadProfile("test");

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber, db,
                                                  profileManager);

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

void testCoordinatorProfileCorrections() {
  MockIPCServer mockServer;
  MockTranscriber mockTranscriber;

  std::string tmpDbDir = "/tmp/punch2pen_test_coord_profile_db";
  std::string tmpProfileDir = "/tmp/punch2pen_test_coord_profile_pm";
  std::filesystem::create_directories(tmpDbDir);
  std::filesystem::create_directories(tmpProfileDir);

  punch2pen::DatabaseManager db;
  db.initialize(tmpDbDir + "/test_corrections.csv");

  punch2pen::ProfileManager profileManager;
  profileManager.setDataDirectory(tmpProfileDir);
  profileManager.loadProfile("test");

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber, db,
                                                  profileManager);

  mockServer.correctionQueue.push_back({"mic", "studio microphone"});

  std::thread worker([&]() { coordinator.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  coordinator.stop();
  worker.join();

  auto dbVocab = db.getVocabulary();
  bool dbHasStudio = std::find(dbVocab.begin(), dbVocab.end(), "studio") != dbVocab.end();
  bool dbHasMicrophone = std::find(dbVocab.begin(), dbVocab.end(), "microphone") != dbVocab.end();
  assert(dbHasStudio && "DB vocabulary should contain 'studio'");
  assert(dbHasMicrophone && "DB vocabulary should contain 'microphone'");

  auto profileCorrections = profileManager.getCorrections();
  assert(profileCorrections.size() == 1 && "ProfileManager should have 1 correction");
  assert(profileCorrections[0].original == "mic" &&
         "Correction original should be 'mic'");
  assert(profileCorrections[0].corrected == "studio microphone" &&
         "Correction corrected should be 'studio microphone'");

  auto profileVocab = profileManager.getVocabulary();
  bool profileHasStudio = std::find(profileVocab.begin(), profileVocab.end(), "studio") != profileVocab.end();
  bool profileHasMicrophone = std::find(profileVocab.begin(), profileVocab.end(), "microphone") != profileVocab.end();
  assert(profileHasStudio && "Profile vocabulary should contain 'studio'");
  assert(profileHasMicrophone && "Profile vocabulary should contain 'microphone'");

  auto &lastVocab = mockTranscriber.lastVocabularyReceived;
  bool transcriberHasStudio = std::find(lastVocab.begin(), lastVocab.end(), "studio") != lastVocab.end();
  bool transcriberHasMicrophone = std::find(lastVocab.begin(), lastVocab.end(), "microphone") != lastVocab.end();
  assert(transcriberHasStudio &&
         "Transcriber vocabulary bias should contain 'studio'");
  assert(transcriberHasMicrophone &&
         "Transcriber vocabulary bias should contain 'microphone'");

  std::cout << "[PASS] testCoordinatorProfileCorrections" << std::endl;

  std::filesystem::remove_all(tmpDbDir);
  std::filesystem::remove_all(tmpProfileDir);
}

void testCoordinatorDrainThreeChunksThenStop() {
  MockIPCServer mockServer;
  MockTranscriber mockTranscriber;

  std::string tmpDir = "/tmp/punch2pen_test_coord_drain";
  std::filesystem::create_directories(tmpDir);
  punch2pen::DatabaseManager db;
  db.initialize(tmpDir + "/test_corrections.csv");

  punch2pen::ProfileManager profileManager;
  profileManager.setDataDirectory(tmpDir);
  profileManager.loadProfile("test");

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber, db,
                                                  profileManager);

  mockServer.queueAudio(std::vector<float>(100, 0.1f), 48000.0, 48000.0);
  mockServer.queueAudio(std::vector<float>(200, 0.2f), 48100.0, 48000.0);
  mockServer.queueAudio(std::vector<float>(300, 0.3f), 48300.0, 48000.0);
  mockServer.queueStop();
  mockServer.queueAudio(std::vector<float>(50, 0.4f), 96000.0, 48000.0);

  std::thread worker([&]() { coordinator.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  coordinator.stop();
  worker.join();

  assert(mockTranscriber.callOrder.size() == 5 &&
         "Error: expected three audio packets, stop, then the next-take packet");
  assert(mockTranscriber.callOrder[0] == "audio");
  assert(mockTranscriber.callOrder[1] == "audio");
  assert(mockTranscriber.callOrder[2] == "audio");
  assert(mockTranscriber.callOrder[3] == "stop" &&
         "Error: finalizeStream ran before every pre-stop audio packet");
  assert(mockTranscriber.callOrder[4] == "audio");
  assert(mockTranscriber.receivedSampleCounts.size() == 4);
  assert(mockTranscriber.receivedSampleCounts[0] == 100);
  assert(mockTranscriber.receivedSampleCounts[1] == 200);
  assert(mockTranscriber.receivedSampleCounts[2] == 300);
  assert(mockTranscriber.receivedSampleCounts[3] == 50);

  std::cout << "[PASS] testCoordinatorDrainThreeChunksThenStop" << std::endl;

  std::filesystem::remove_all(tmpDir);
}

void testCoordinatorServicesCorrectionsDuringAudio() {
  MockIPCServer mockServer;
  MockTranscriber mockTranscriber;

  std::string tmpDir = "/tmp/punch2pen_test_coord_corr_audio";
  std::filesystem::create_directories(tmpDir);
  punch2pen::DatabaseManager db;
  db.initialize(tmpDir + "/test_corrections.csv");

  punch2pen::ProfileManager profileManager;
  profileManager.setDataDirectory(tmpDir);
  profileManager.loadProfile("test");

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber, db,
                                                  profileManager);

  mockServer.queueAudio(std::vector<float>(100, 0.1f), 0.0, 48000.0);
  mockServer.queueAudio(std::vector<float>(100, 0.2f), 100.0, 48000.0);
  mockServer.queueAudio(std::vector<float>(100, 0.3f), 200.0, 48000.0);
  mockServer.correctionQueue.push_back({"hello", "world"});

  std::thread worker([&]() { coordinator.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  coordinator.stop();
  worker.join();

  assert(mockTranscriber.setVocabularyBiasCalled);
  assert(mockTranscriber.receivedSampleCounts.size() == 3);
  assert(mockTranscriber.audioCountAtBias == 1 &&
         "Error: correction waited until the audio queue drained");
  assert(mockTranscriber.callOrder.size() >= 4);
  assert(mockTranscriber.callOrder[0] == "audio");
  assert(mockTranscriber.callOrder[1] == "correction");

  std::cout << "[PASS] testCoordinatorServicesCorrectionsDuringAudio"
            << std::endl;

  std::filesystem::remove_all(tmpDir);
}

int main() {
  testCoordinatorRouting();
  testCoordinatorCorrections();
  testCoordinatorProfileCorrections();
  testCoordinatorDrainThreeChunksThenStop();
  testCoordinatorServicesCorrectionsDuringAudio();
  std::cout << "All TranscriptionCoordinator tests passed!" << std::endl;
  return 0;
}
