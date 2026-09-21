#pragma once

#include "CloudProfileClient.h"
#include "Dictionary.h"
#include "ProfileService.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace punch2pen {

struct AccountConfig {
  std::string dataDir;            // ~/.punch2pen
  std::string profileApiUrl;      // empty => sign-in unavailable, stay local
  std::string deviceName = "punch2pen engine";
  std::string platform = "unknown";
};

// Free/lite vs paid/pro, as the engine sees it.
//
// free : no account, no network. Corrections live in a session dictionary
//        that biases this engine run and is dropped on restart or sign-out.
// paid : signed in with an active profile (seat). Corrections write that
//        profile's dictionary: cached at <dataDir>/profiles/<id>.json and
//        pushed to the profile API so it follows the artist to another
//        DAW or room. Switching profiles swaps the whole dictionary, so
//        one artist never trains another's.
class AccountManager : public ProfileService {
public:
  AccountManager(AccountConfig config,
                 std::unique_ptr<CloudProfileClient> cloud);
  ~AccountManager() override;

  // Loads account.json + dictionary caches and starts the worker thread.
  // A saved session schedules a background refresh; free tier stays silent.
  void start();
  void stop();

  // First half of start(), without the worker thread. Tests drive the
  // queued refresh with drainQueueNow().
  void loadSavedState();

  // Receives the JSON ProfileStatus to broadcast to plugins. Called from
  // the worker thread and from recordCorrection's caller.
  void setStatusSink(std::function<void(const std::string &)> sink);

  // ProfileService
  void recordCorrection(const std::string &original,
                        const std::string &corrected) override;
  std::vector<std::string> vocabularyForBias() const override;
  uint64_t dictionaryRevision() const override;
  void postCommand(const std::string &json) override;

  // Case-sensitive dictionary map applied to each raw transcript word
  // before it leaves the engine.
  std::string mapWord(const std::string &word) const;

  std::string statusJson() const;
  std::string tier() const;
  bool signedIn() const;
  std::string activeProfileId() const;
  std::size_t dictionaryEntryCount() const;

  // Synchronous entry points used by the worker and by unit tests.
  void handleCommandNow(const std::string &json);
  void drainQueueNow();
  void flushPendingNow();

  static constexpr const char *kAccountFile = "account.json";

private:
  struct Session {
    std::string token;
    std::string userId;
    std::string email;
  };

  struct LoginState {
    std::string stage = "idle"; // idle | code_sent | verifying | error
    std::string email;
    std::string echoedCode;
    std::string message;
  };

  void workerLoop();
  void enqueueInternal(const std::string &json);

  void doStatus();
  void doLoginStart(const std::string &email);
  void doLoginVerify(const std::string &email, const std::string &code);
  void doLogout(bool tellServer);
  void doSetActive(const std::string &profileId, bool announce);
  void doRefresh();
  void doFlush();

  bool cloudAvailable() const;
  bool isPaidLocked() const;
  const CloudProfile *findProfileLocked(const std::string &id) const;
  std::string profileCachePath(const std::string &profileId) const;
  void loadActiveDictionaryLocked();
  void saveActiveDictionaryLocked() const;
  void adoptProfilesLocked(std::vector<CloudProfile> profiles);
  void handleUnauthorizedLocked(const std::string &reason);
  void bumpRevisionLocked();
  void clearLocalPaidStateLocked(bool deleteCaches);

  bool loadAccountFile();
  void saveAccountFileLocked() const;
  void removeAccountFileLocked() const;
  void emitStatus();
  void emitStatusLocked();
  std::string statusJsonLocked() const;

  AccountConfig config_;
  std::unique_ptr<CloudProfileClient> cloud_;
  std::function<void(const std::string &)> statusSink_;

  mutable std::mutex mutex_;
  Session session_;
  std::vector<CloudProfile> profiles_;
  std::string activeProfileId_;
  Dictionary sessionDictionary_;
  Dictionary profileDictionary_;
  std::string sync_ = "local"; // local | cached | synced | offline | error
  LoginState login_;
  std::string message_;
  std::string lastLoggedTier_;
  std::atomic<uint64_t> revision_{0};

  std::mutex queueMutex_;
  std::condition_variable queueCv_;
  std::deque<std::string> queue_;
  std::thread worker_;
  std::atomic<bool> running_{false};
};

} // namespace punch2pen
