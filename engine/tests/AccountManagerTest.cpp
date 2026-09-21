#include "../src/AccountManager.h"
#include "../src/CloudProfileClient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

using json = nlohmann::json;

namespace {

punch2pen::CloudProfile makeProfile(const std::string &id,
                                    const std::string &name,
                                    const std::string &workspace,
                                    const std::string &status = "active") {
  punch2pen::CloudProfile p;
  p.id = id;
  p.name = name;
  p.workspaceId = "ws_" + workspace;
  p.workspaceName = workspace;
  p.role = "artist";
  p.status = status;
  return p;
}

// Scripted profile API. Counts every call so "free makes no network calls"
// is a real assertion, not a hope.
class MockCloud : public punch2pen::CloudProfileClient {
public:
  int calls = 0;
  std::vector<std::string> log;
  bool offline = false;
  bool unauthorized = false;
  std::string issuedToken = "p2p_test_token";
  std::vector<punch2pen::CloudProfile> profiles;
  std::map<std::string, std::vector<punch2pen::Dictionary::Entry>> dictionaries;
  std::vector<std::pair<std::string, std::string>> pushes; // profileId, original

  punch2pen::LoginStartResult startLogin(const std::string &email) override {
    ++calls;
    log.push_back("start:" + email);
    punch2pen::LoginStartResult r;
    if (offline) {
      r.networkError = true;
      r.message = "connect failed";
      return r;
    }
    r.ok = true;
    r.httpStatus = 200;
    r.delivery = "echo";
    r.echoedCode = "123456";
    return r;
  }

  punch2pen::LoginVerifyResult verifyLogin(const std::string &email,
                                           const std::string &code,
                                           const std::string &,
                                           const std::string &) override {
    ++calls;
    log.push_back("verify:" + email + ":" + code);
    punch2pen::LoginVerifyResult r;
    if (code != "123456") {
      r.httpStatus = 400;
      r.error = "code_invalid";
      r.message = "That code did not match";
      return r;
    }
    r.ok = true;
    r.httpStatus = 200;
    r.token = issuedToken;
    r.userId = "user_1";
    r.email = email;
    r.profiles = profiles;
    return r;
  }

  punch2pen::CloudResult logout(const std::string &) override {
    ++calls;
    log.push_back("logout");
    punch2pen::CloudResult r;
    r.ok = true;
    r.httpStatus = 200;
    return r;
  }

  punch2pen::MeResult me(const std::string &token) override {
    ++calls;
    log.push_back("me");
    punch2pen::MeResult r;
    if (offline) {
      r.networkError = true;
      return r;
    }
    if (unauthorized || token != issuedToken) {
      r.httpStatus = 401;
      r.error = "unauthorized";
      return r;
    }
    r.ok = true;
    r.httpStatus = 200;
    r.profiles = profiles;
    return r;
  }

  punch2pen::DictionaryResult fetchDictionary(const std::string &token,
                                              const std::string &profileId) override {
    ++calls;
    log.push_back("dict:" + profileId);
    punch2pen::DictionaryResult r;
    if (offline) {
      r.networkError = true;
      return r;
    }
    if (unauthorized || token != issuedToken) {
      r.httpStatus = 401;
      r.error = "unauthorized";
      return r;
    }
    r.ok = true;
    r.httpStatus = 200;
    r.profileId = profileId;
    r.version = 5;
    r.entries = dictionaries[profileId];
    return r;
  }

  punch2pen::PushResult pushCorrection(const std::string &token,
                                       const std::string &profileId,
                                       const std::string &original,
                                       const std::string &corrected) override {
    ++calls;
    log.push_back("push:" + profileId + ":" + original);
    punch2pen::PushResult r;
    if (offline) {
      r.networkError = true;
      return r;
    }
    if (unauthorized || token != issuedToken) {
      r.httpStatus = 401;
      r.error = "unauthorized";
      return r;
    }
    pushes.emplace_back(profileId, original);
    auto &entries = dictionaries[profileId];
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&](const auto &e) { return e.original == original; });
    if (it == entries.end())
      entries.push_back({original, corrected, 1, 1, false});
    else {
      it->corrected = corrected;
      it->count += 1;
    }
    r.ok = true;
    r.httpStatus = 200;
    r.version = 6;
    r.count = 1;
    return r;
  }
};

struct Fixture {
  std::string dir;
  MockCloud *cloud = nullptr;
  std::unique_ptr<punch2pen::AccountManager> account;
  std::vector<json> statuses;

  explicit Fixture(const std::string &name, bool withApi = true) {
    dir = "/tmp/punch2pen_test_account_" + name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    reopen(withApi);
  }

  void reopen(bool withApi = true) {
    account.reset();
    punch2pen::AccountConfig cfg;
    cfg.dataDir = dir;
    cfg.profileApiUrl = withApi ? "https://profiles.example.test" : "";
    cfg.deviceName = "test-mac";
    cfg.platform = "test";
    std::unique_ptr<MockCloud> mock;
    if (withApi) {
      mock = std::make_unique<MockCloud>();
      mock->profiles = {makeProfile("prof_a", "Keegan", "Studio X"),
                        makeProfile("prof_b", "Artist B", "Studio X")};
      mock->dictionaries["prof_a"] = {{"nah", "hell nah", 2, 10, false}};
      mock->dictionaries["prof_b"] = {{"yo", "YO", 1, 10, false}};
    }
    cloud = mock.get();
    account = std::make_unique<punch2pen::AccountManager>(cfg, std::move(mock));
    account->setStatusSink([this](const std::string &s) {
      statuses.push_back(json::parse(s));
    });
  }

  json lastStatus() const {
    assert(!statuses.empty() && "expected at least one ProfileStatus");
    return statuses.back();
  }

  void signIn() {
    account->handleCommandNow(R"({"op":"login_start","email":"keegan@example.test"})");
    assert(lastStatus()["login"]["stage"] == "code_sent");
    assert(lastStatus()["login"]["echoedCode"] == "123456");
    account->handleCommandNow(
        R"({"op":"login_verify","email":"keegan@example.test","code":"123456"})");
    account->drainQueueNow();
  }

  ~Fixture() {
    account.reset();
    std::filesystem::remove_all(dir);
  }
};

bool fileIsOwnerOnly(const std::string &path) {
#if !defined(_WIN32)
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0)
    return false;
  return (st.st_mode & 077) == 0;
#else
  (void)path;
  return true;
#endif
}

void testFreeTierStaysLocalWithoutApi() {
  Fixture f("free_noapi", /*withApi=*/false);
  f.account->loadSavedState();
  f.account->drainQueueNow();

  f.account->handleCommandNow(R"({"op":"status"})");
  json s = f.lastStatus();
  assert(s["type"] == "profileStatus");
  assert(s["tier"] == "free");
  assert(s["cloudAvailable"] == false);
  assert(s["signedIn"] == false);
  assert(s["dictionary"]["scope"] == "session");
  assert(s["dictionary"]["entries"] == 0);
  assert(s["sync"] == "local");

  f.account->recordCorrection("helo", "hello");
  f.account->drainQueueNow();
  assert(f.account->tier() == "free");
  assert(f.account->mapWord("helo") == "hello");
  assert(f.account->mapWord("helo!") == "hello!");
  assert(f.account->dictionaryEntryCount() == 1);
  assert(f.account->dictionaryRevision() >= 2);
  s = f.lastStatus();
  assert(s["dictionary"]["entries"] == 1);
  assert(s["dictionary"]["biasTerms"] == 1);

  // Nothing persisted: session-only, reset on restart.
  assert(!std::filesystem::exists(f.dir + "/account.json"));
  assert(!std::filesystem::exists(f.dir + "/profiles"));

  f.account->handleCommandNow(R"({"op":"login_start","email":"x@y.z"})");
  s = f.lastStatus();
  assert(s["login"]["stage"] == "error");
  assert(std::string(s["login"]["message"]).find("not available") !=
         std::string::npos);

  // Restart drops the session dictionary.
  f.reopen(false);
  f.account->loadSavedState();
  assert(f.account->mapWord("helo") == "helo");
  assert(f.account->dictionaryEntryCount() == 0);

  std::cout << "[PASS] testFreeTierStaysLocalWithoutApi" << std::endl;
}

void testFreeTierWithApiMakesNoNetworkCallsUntilLogin() {
  Fixture f("free_api");
  f.account->loadSavedState();
  f.account->drainQueueNow();
  f.account->recordCorrection("mic", "microphone");
  f.account->handleCommandNow(R"({"op":"status"})");
  f.account->drainQueueNow();
  assert(f.cloud->calls == 0 && "free tier must not touch the profile API");
  assert(f.lastStatus()["tier"] == "free");
  assert(f.lastStatus()["cloudAvailable"] == true);
  std::cout << "[PASS] testFreeTierWithApiMakesNoNetworkCallsUntilLogin"
            << std::endl;
}

void testLoginFlipsToPaidAndCarriesSessionCorrections() {
  Fixture f("login");
  f.account->loadSavedState();
  f.account->recordCorrection("helo", "hello"); // made before signing in
  f.account->drainQueueNow();
  assert(f.cloud->calls == 0);

  f.signIn();

  json s = f.lastStatus();
  assert(s["tier"] == "paid");
  assert(s["signedIn"] == true);
  assert(s["email"] == "keegan@example.test");
  assert(s["activeProfile"]["id"] == "prof_a");
  assert(s["activeProfile"]["name"] == "Keegan");
  assert(s["activeProfile"]["workspaceName"] == "Studio X");
  assert(s["profiles"].size() == 2);
  assert(s["dictionary"]["scope"] == "profile");
  assert(s["sync"] == "synced");
  assert(s["login"]["stage"] == "idle");

  // Cloud dictionary for A landed, and the pre-login correction was pushed.
  assert(f.account->mapWord("nah") == "hell nah");
  assert(f.account->mapWord("helo") == "hello");
  assert(f.cloud->pushes.size() == 1);
  assert(f.cloud->pushes[0].first == "prof_a");
  assert(f.cloud->pushes[0].second == "helo");
  assert(s["dictionary"]["pending"] == 0);

  // Session persisted for the next engine start, owner-only.
  const std::string accountPath = f.dir + "/account.json";
  assert(std::filesystem::exists(accountPath));
  assert(fileIsOwnerOnly(accountPath));
  const std::string cachePath = f.dir + "/profiles/prof_a.json";
  assert(std::filesystem::exists(cachePath));
  assert(fileIsOwnerOnly(cachePath));

  std::cout << "[PASS] testLoginFlipsToPaidAndCarriesSessionCorrections"
            << std::endl;
}

void testWrongCodeStaysFree() {
  Fixture f("badcode");
  f.account->loadSavedState();
  f.account->handleCommandNow(R"({"op":"login_start","email":"keegan@example.test"})");
  f.account->handleCommandNow(
      R"({"op":"login_verify","email":"keegan@example.test","code":"000000"})");
  json s = f.lastStatus();
  assert(s["tier"] == "free");
  assert(s["signedIn"] == false);
  assert(s["login"]["stage"] == "error");
  assert(s["login"]["message"] == "That code did not match");
  assert(!std::filesystem::exists(f.dir + "/account.json"));
  std::cout << "[PASS] testWrongCodeStaysFree" << std::endl;
}

void testPaidCorrectionWritesProfileDictionaryAndSyncs() {
  Fixture f("paid_corr");
  f.account->loadSavedState();
  f.signIn();
  const int callsAfterLogin = f.cloud->calls;

  f.account->recordCorrection("Kegan", "Keegan");
  // Cached before any network round trip.
  punch2pen::Dictionary cache;
  assert(cache.loadFromFile(f.dir + "/profiles/prof_a.json"));
  assert(cache.mapWord("Kegan") == "Keegan");
  assert(cache.pendingEntries().size() == 1);

  f.account->drainQueueNow();
  assert(f.cloud->calls > callsAfterLogin);
  assert(f.cloud->pushes.back().first == "prof_a");
  assert(f.cloud->pushes.back().second == "Kegan");
  assert(cache.loadFromFile(f.dir + "/profiles/prof_a.json"));
  assert(cache.pendingEntries().empty() && "push must clear pendingSync");
  assert(f.lastStatus()["dictionary"]["pending"] == 0);
  assert(f.lastStatus()["sync"] == "synced");

  const auto vocab = f.account->vocabularyForBias();
  assert(std::find(vocab.begin(), vocab.end(), "Keegan") != vocab.end());
  assert(std::find(vocab.begin(), vocab.end(), "hell") != vocab.end());

  std::cout << "[PASS] testPaidCorrectionWritesProfileDictionaryAndSyncs"
            << std::endl;
}

void testSeatSwitchIsolatesDictionaries() {
  Fixture f("seats");
  f.account->loadSavedState();
  f.signIn();
  assert(f.account->mapWord("nah") == "hell nah");

  f.account->handleCommandNow(R"({"op":"set_active","profileId":"prof_b"})");
  f.account->drainQueueNow();
  json s = f.lastStatus();
  assert(s["activeProfile"]["id"] == "prof_b");
  assert(s["activeProfile"]["name"] == "Artist B");
  assert(f.account->mapWord("nah") == "nah" &&
         "Artist A's dictionary must not leak into Artist B");
  assert(f.account->mapWord("yo") == "YO");

  f.account->recordCorrection("brr", "brrr");
  f.account->drainQueueNow();
  assert(f.cloud->pushes.back().first == "prof_b");
  assert(f.cloud->dictionaries["prof_a"].size() == 1 &&
         "Artist B's correction must not train Artist A");

  // Back to A: B's words are gone, A's are back.
  f.account->handleCommandNow(R"({"op":"set_active","profileId":"prof_a"})");
  f.account->drainQueueNow();
  assert(f.account->mapWord("brr") == "brr");
  assert(f.account->mapWord("nah") == "hell nah");

  f.account->handleCommandNow(R"({"op":"set_active","profileId":"prof_zzz"})");
  assert(f.account->activeProfileId() == "prof_a");
  assert(std::string(f.lastStatus()["message"]).find("not available") !=
         std::string::npos);

  std::cout << "[PASS] testSeatSwitchIsolatesDictionaries" << std::endl;
}

void testRestartRestoresPaidFromCacheWhenOffline() {
  Fixture f("restart");
  f.account->loadSavedState();
  f.signIn();
  f.account->recordCorrection("Kegan", "Keegan");
  f.account->drainQueueNow();

  f.reopen();
  f.cloud->offline = true;
  f.account->loadSavedState(); // queues refresh
  assert(f.account->tier() == "paid");
  assert(f.account->activeProfileId() == "prof_a");
  assert(f.account->mapWord("Kegan") == "Keegan" && "cache restored");
  assert(f.account->mapWord("nah") == "hell nah");

  f.account->drainQueueNow(); // refresh fails: stay paid, mark offline
  json s = f.lastStatus();
  assert(s["tier"] == "paid");
  assert(s["sync"] == "offline");

  // Offline correction is cached as pending and pushed once we are back.
  f.account->recordCorrection("hel", "hell");
  f.account->drainQueueNow();
  assert(f.lastStatus()["dictionary"]["pending"] == 1);
  f.cloud->offline = false;
  f.account->handleCommandNow(R"({"op":"refresh"})");
  f.account->drainQueueNow();
  assert(f.lastStatus()["dictionary"]["pending"] == 0);
  assert(f.lastStatus()["sync"] == "synced");
  assert(f.cloud->pushes.back().second == "hel");

  std::cout << "[PASS] testRestartRestoresPaidFromCacheWhenOffline" << std::endl;
}

void testExpiredSessionFallsBackToFree() {
  Fixture f("expired");
  f.account->loadSavedState();
  f.signIn();
  f.reopen();
  f.cloud->unauthorized = true;
  f.account->loadSavedState();
  assert(f.account->tier() == "paid"); // from cache, until the API says no
  f.account->drainQueueNow();
  json s = f.lastStatus();
  assert(s["tier"] == "free");
  assert(s["signedIn"] == false);
  assert(std::string(s["message"]).find("Sign in again") != std::string::npos);
  assert(!std::filesystem::exists(f.dir + "/account.json"));
  assert(!std::filesystem::exists(f.dir + "/profiles/prof_a.json"));
  std::cout << "[PASS] testExpiredSessionFallsBackToFree" << std::endl;
}

void testLogoutResetsToFreeAndForgetsDictionary() {
  Fixture f("logout");
  f.account->loadSavedState();
  f.signIn();
  f.account->recordCorrection("Kegan", "Keegan");
  f.account->drainQueueNow();

  f.account->handleCommandNow(R"({"op":"logout"})");
  json s = f.lastStatus();
  assert(s["tier"] == "free");
  assert(s["signedIn"] == false);
  assert(s["activeProfile"].is_null());
  assert(s["dictionary"]["entries"] == 0);
  assert(f.account->mapWord("Kegan") == "Kegan");
  assert(f.account->mapWord("nah") == "nah");
  assert(!std::filesystem::exists(f.dir + "/account.json"));
  assert(!std::filesystem::exists(f.dir + "/profiles/prof_a.json"));
  assert(f.cloud->log.back() == "logout");
  std::cout << "[PASS] testLogoutResetsToFreeAndForgetsDictionary" << std::endl;
}

void testSuspendedSeatIsNotPaid() {
  Fixture f("suspended");
  f.cloud->profiles = {makeProfile("prof_a", "Keegan", "Studio X", "suspended")};
  f.account->loadSavedState();
  f.signIn();
  json s = f.lastStatus();
  assert(s["signedIn"] == true);
  assert(s["tier"] == "free" && "suspended workspace must not unlock paid");
  assert(s["dictionary"]["scope"] == "session");
  std::cout << "[PASS] testSuspendedSeatIsNotPaid" << std::endl;
}

void testAccountFileFromOtherDeploymentIsIgnored() {
  Fixture f("otherdeploy");
  f.account->loadSavedState();
  f.signIn();
  f.account.reset();

  punch2pen::AccountConfig cfg;
  cfg.dataDir = f.dir;
  cfg.profileApiUrl = "https://elsewhere.example.test";
  punch2pen::AccountManager other(cfg, std::make_unique<MockCloud>());
  other.loadSavedState();
  assert(!other.signedIn() && "token from another API must not be reused");
  assert(other.tier() == "free");
  std::cout << "[PASS] testAccountFileFromOtherDeploymentIsIgnored" << std::endl;
}

} // namespace

int main() {
  testFreeTierStaysLocalWithoutApi();
  testFreeTierWithApiMakesNoNetworkCallsUntilLogin();
  testLoginFlipsToPaidAndCarriesSessionCorrections();
  testWrongCodeStaysFree();
  testPaidCorrectionWritesProfileDictionaryAndSyncs();
  testSeatSwitchIsolatesDictionaries();
  testRestartRestoresPaidFromCacheWhenOffline();
  testExpiredSessionFallsBackToFree();
  testLogoutResetsToFreeAndForgetsDictionary();
  testSuspendedSeatIsNotPaid();
  testAccountFileFromOtherDeploymentIsIgnored();
  std::cout << "All AccountManager tests passed!" << std::endl;
  return 0;
}
