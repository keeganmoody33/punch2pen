#include "../src/AccountManager.h"
#include "../src/IPCServerInterface.h"
#include "../src/ProfileService.h"
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

  bool hasPendingProfileCommand() override {
    return !profileCommandQueue.empty();
  }

  std::string popProfileCommand() override {
    if (profileCommandQueue.empty())
      return {};
    auto c = profileCommandQueue.front();
    profileCommandQueue.erase(profileCommandQueue.begin());
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
  std::vector<std::string> profileCommandQueue;
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

// Records what the coordinator hands to the account layer.
class RecordingProfileService : public punch2pen::ProfileService {
public:
  void recordCorrection(const std::string &original,
                        const std::string &corrected) override {
    corrections.push_back({original, corrected});
    ++revision;
  }
  std::vector<std::string> vocabularyForBias() const override {
    return vocabulary;
  }
  uint64_t dictionaryRevision() const override { return revision; }
  void postCommand(const std::string &json) override {
    commands.push_back(json);
  }

  std::vector<std::pair<std::string, std::string>> corrections;
  std::vector<std::string> commands;
  std::vector<std::string> vocabulary{"studio", "microphone"};
  uint64_t revision = 0;
};

namespace {
punch2pen::AccountConfig freeConfig(const std::string &dir) {
  punch2pen::AccountConfig cfg;
  cfg.dataDir = dir;
  cfg.profileApiUrl = "";
  return cfg;
}

bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}
} // namespace

void testCoordinatorRouting() {
  MockIPCServer mockServer;
  MockTranscriber mockTranscriber;

  std::string tmpDir = "/tmp/punch2pen_test_coord";
  std::filesystem::create_directories(tmpDir);
  punch2pen::AccountManager account(freeConfig(tmpDir), nullptr);

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber,
                                                  account);

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
  assert(!mockTranscriber.setVocabularyBiasCalled &&
         "Error: no correction and no dictionary, yet bias was applied");

  std::cout << "[PASS] testCoordinatorRouting" << std::endl;

  std::filesystem::remove_all(tmpDir);
}

void testCoordinatorCorrectionsReachFreeSessionDictionary() {
  MockIPCServer mockServer;
  MockTranscriber mockTranscriber;

  std::string tmpDir = "/tmp/punch2pen_test_coord_corr";
  std::filesystem::create_directories(tmpDir);
  punch2pen::AccountManager account(freeConfig(tmpDir), nullptr);

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber,
                                                  account);

  mockServer.correctionQueue.push_back({"mic", "studio microphone"});

  std::thread worker([&]() { coordinator.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  coordinator.stop();
  worker.join();

  assert(mockTranscriber.setVocabularyBiasCalled &&
         "Error: Coordinator did not update vocabulary after correction!");
  assert(contains(mockTranscriber.lastVocabularyReceived, "studio"));
  assert(contains(mockTranscriber.lastVocabularyReceived, "microphone"));
  assert(account.tier() == "free");
  assert(account.dictionaryEntryCount() == 1);
  assert(account.mapWord("mic") == "studio microphone" &&
         "Error: session dictionary did not map the corrected word");
  // Free tier writes nothing to disk: session-only, reset on restart.
  assert(!std::filesystem::exists(tmpDir + "/account.json"));
  assert(!std::filesystem::exists(tmpDir + "/profiles"));
  assert(!std::filesystem::exists(tmpDir + "/corrections.csv"));

  std::cout << "[PASS] testCoordinatorCorrectionsReachFreeSessionDictionary"
            << std::endl;

  std::filesystem::remove_all(tmpDir);
}

void testCoordinatorForwardsProfileCommands() {
  MockIPCServer mockServer;
  MockTranscriber mockTranscriber;
  RecordingProfileService profiles;

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber,
                                                  profiles);

  mockServer.profileCommandQueue.push_back(R"({"op":"status"})");
  mockServer.profileCommandQueue.push_back(
      R"({"op":"login_start","email":"a@b.co"})");
  mockServer.correctionQueue.push_back({"helo", "hello"});

  std::thread worker([&]() { coordinator.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  coordinator.stop();
  worker.join();

  assert(profiles.commands.size() == 2 &&
         "Error: profile commands were not forwarded to the account layer");
  assert(profiles.commands[0] == R"({"op":"status"})");
  assert(profiles.corrections.size() == 1);
  assert(profiles.corrections[0].second == "hello");
  assert(mockTranscriber.setVocabularyBiasCalled &&
         "Error: revision bump did not reapply vocabulary bias");
  assert(mockTranscriber.lastVocabularyReceived == profiles.vocabulary);

  std::cout << "[PASS] testCoordinatorForwardsProfileCommands" << std::endl;
}

void testCoordinatorDrainThreeChunksThenStop() {
  MockIPCServer mockServer;
  MockTranscriber mockTranscriber;

  std::string tmpDir = "/tmp/punch2pen_test_coord_drain";
  std::filesystem::create_directories(tmpDir);
  punch2pen::AccountManager account(freeConfig(tmpDir), nullptr);

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber,
                                                  account);

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
  punch2pen::AccountManager account(freeConfig(tmpDir), nullptr);

  Punch2Pen::TranscriptionCoordinator coordinator(mockServer, mockTranscriber,
                                                  account);

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
  testCoordinatorCorrectionsReachFreeSessionDictionary();
  testCoordinatorForwardsProfileCommands();
  testCoordinatorDrainThreeChunksThenStop();
  testCoordinatorServicesCorrectionsDuringAudio();
  std::cout << "All TranscriptionCoordinator tests passed!" << std::endl;
  return 0;
}
