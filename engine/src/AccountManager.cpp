#include "AccountManager.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace punch2pen {

namespace {

using json = nlohmann::json;

int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

json profileToJson(const CloudProfile &p) {
  json out;
  out["id"] = p.id;
  out["name"] = p.name;
  out["workspaceId"] = p.workspaceId;
  out["workspaceName"] = p.workspaceName;
  out["role"] = p.role;
  out["status"] = p.status;
  out["dictionaryVersion"] = p.dictionaryVersion;
  return out;
}

CloudProfile profileFromJson(const json &item) {
  CloudProfile p;
  if (!item.is_object())
    return p;
  p.id = item.value("id", std::string{});
  p.name = item.value("name", std::string{});
  p.workspaceId = item.value("workspaceId", std::string{});
  p.workspaceName = item.value("workspaceName", std::string{});
  p.role = item.value("role", std::string{"artist"});
  p.status = item.value("status", std::string{"active"});
  p.dictionaryVersion = item.value("dictionaryVersion", int64_t{0});
  return p;
}

void restrictToOwner(const std::string &path) {
#if !defined(_WIN32)
  ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
#else
  (void)path;
#endif
}

std::string safeFileComponent(const std::string &id) {
  std::string out;
  for (const char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    out.push_back(ok ? c : '_');
  }
  return out.empty() ? std::string{"profile"} : out;
}

std::string transportMessage(const CloudResult &r, const char *fallback) {
  if (r.networkError)
    return "Cannot reach the profile API. Check your connection.";
  return r.message.empty() ? fallback : r.message;
}

} // namespace

AccountManager::AccountManager(AccountConfig config,
                               std::unique_ptr<CloudProfileClient> cloud)
    : config_(std::move(config)), cloud_(std::move(cloud)) {}

AccountManager::~AccountManager() { stop(); }

// ── lifecycle ────────────────────────────────────────────────────────────

void AccountManager::loadSavedState() {
  loadAccountFile();
  bool refresh = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isPaidLocked()) {
      loadActiveDictionaryLocked();
      sync_ = "cached";
      refresh = cloudAvailable();
    }
    bumpRevisionLocked();
  }
  if (refresh)
    enqueueInternal(R"({"op":"refresh"})");
}

void AccountManager::start() {
  loadSavedState();
  running_.store(true);
  worker_ = std::thread(&AccountManager::workerLoop, this);
  emitStatus();
}

void AccountManager::stop() {
  if (!running_.exchange(false))
    return;
  queueCv_.notify_all();
  if (worker_.joinable())
    worker_.join();
}

void AccountManager::setStatusSink(
    std::function<void(const std::string &)> sink) {
  std::lock_guard<std::mutex> lock(mutex_);
  statusSink_ = std::move(sink);
}

void AccountManager::workerLoop() {
  while (true) {
    std::string command;
    {
      std::unique_lock<std::mutex> lock(queueMutex_);
      queueCv_.wait(lock, [this] {
        return !running_.load() || !queue_.empty();
      });
      if (!running_.load())
        return;
      command = std::move(queue_.front());
      queue_.pop_front();
    }
    handleCommandNow(command);
  }
}

void AccountManager::enqueueInternal(const std::string &json) {
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.push_back(json);
  }
  queueCv_.notify_one();
}

void AccountManager::postCommand(const std::string &json) {
  enqueueInternal(json);
}

void AccountManager::drainQueueNow() {
  while (true) {
    std::string command;
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      if (queue_.empty())
        return;
      command = std::move(queue_.front());
      queue_.pop_front();
    }
    handleCommandNow(command);
  }
}

// ── ProfileService ───────────────────────────────────────────────────────

void AccountManager::recordCorrection(const std::string &original,
                                      const std::string &corrected) {
  bool flush = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = nowMs();
    if (isPaidLocked()) {
      profileDictionary_.addCorrection(original, corrected, now, true);
      saveActiveDictionaryLocked();
      flush = cloudAvailable();
    } else {
      sessionDictionary_.addCorrection(original, corrected, now, false);
    }
    bumpRevisionLocked();
  }
  if (flush)
    enqueueInternal(R"({"op":"_flush"})");
  enqueueInternal(R"({"op":"status"})");
}

std::vector<std::string> AccountManager::vocabularyForBias() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return isPaidLocked() ? profileDictionary_.vocabularyForBias()
                        : sessionDictionary_.vocabularyForBias();
}

uint64_t AccountManager::dictionaryRevision() const {
  return revision_.load();
}

std::string AccountManager::mapWord(const std::string &word) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return isPaidLocked() ? profileDictionary_.mapWord(word)
                        : sessionDictionary_.mapWord(word);
}

// ── introspection ────────────────────────────────────────────────────────

std::string AccountManager::statusJson() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return statusJsonLocked();
}

std::string AccountManager::tier() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return isPaidLocked() ? "paid" : "free";
}

bool AccountManager::signedIn() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !session_.token.empty();
}

std::string AccountManager::activeProfileId() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return activeProfileId_;
}

std::size_t AccountManager::dictionaryEntryCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return isPaidLocked() ? profileDictionary_.entryCount()
                        : sessionDictionary_.entryCount();
}

// ── commands ─────────────────────────────────────────────────────────────

void AccountManager::handleCommandNow(const std::string &raw) {
  json cmd;
  try {
    cmd = json::parse(raw);
  } catch (const std::exception &) {
    cmd = json::object();
  }
  const std::string op =
      cmd.is_object() ? cmd.value("op", std::string{}) : std::string{};

  if (op == "status") {
    doStatus();
  } else if (op == "login_start") {
    doLoginStart(cmd.value("email", std::string{}));
  } else if (op == "login_verify") {
    doLoginVerify(cmd.value("email", std::string{}),
                  cmd.value("code", std::string{}));
  } else if (op == "logout") {
    doLogout(true);
  } else if (op == "set_active") {
    doSetActive(cmd.value("profileId", std::string{}), true);
  } else if (op == "refresh") {
    doRefresh();
  } else if (op == "_flush") {
    doFlush();
  } else {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      message_ = "Unknown profile command" + (op.empty() ? "" : ": " + op);
    }
    emitStatus();
  }
}

void AccountManager::flushPendingNow() { doFlush(); }

void AccountManager::doStatus() { emitStatus(); }

void AccountManager::doLoginStart(const std::string &emailRaw) {
  std::string email = emailRaw;
  while (!email.empty() && std::isspace(static_cast<unsigned char>(email.back())))
    email.pop_back();
  while (!email.empty() && std::isspace(static_cast<unsigned char>(email.front())))
    email.erase(email.begin());

  if (!cloudAvailable()) {
    std::lock_guard<std::mutex> lock(mutex_);
    login_.stage = "error";
    login_.email = email;
    login_.message =
        "Sign-in is not available in this build: no profile API configured.";
    return emitStatusLocked();
  }
  if (email.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    login_.stage = "error";
    login_.message = "Enter the email your seat was created with.";
    return emitStatusLocked();
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    login_.stage = "sending";
    login_.email = email;
    login_.echoedCode.clear();
    login_.message.clear();
  }
  emitStatus();

  const LoginStartResult result = cloud_->startLogin(email);

  std::lock_guard<std::mutex> lock(mutex_);
  if (result.ok) {
    login_.stage = "code_sent";
    login_.echoedCode = result.echoedCode;
    login_.message = result.delivery == "echo"
                         ? "Development deployment echoed the code."
                         : "Check your email for a six-digit code.";
  } else {
    login_.stage = "error";
    login_.message = transportMessage(result, "Could not start sign-in.");
  }
  emitStatusLocked();
}

void AccountManager::doLoginVerify(const std::string &email,
                                   const std::string &codeRaw) {
  std::string code;
  for (const char c : codeRaw)
    if (!std::isspace(static_cast<unsigned char>(c)))
      code.push_back(c);

  if (!cloudAvailable()) {
    std::lock_guard<std::mutex> lock(mutex_);
    login_.stage = "error";
    login_.message =
        "Sign-in is not available in this build: no profile API configured.";
    return emitStatusLocked();
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    login_.stage = "verifying";
    login_.message.clear();
  }
  emitStatus();

  const LoginVerifyResult result =
      cloud_->verifyLogin(email, code, config_.deviceName, config_.platform);

  std::vector<Dictionary::Entry> carried;
  std::string active;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!result.ok) {
      login_.stage = "error";
      login_.message = transportMessage(result, "That code did not work.");
      return emitStatusLocked();
    }
    session_.token = result.token;
    session_.userId = result.userId;
    session_.email = result.email.empty() ? email : result.email;
    adoptProfilesLocked(result.profiles);
    login_ = LoginState{};
    message_.clear();
    sync_ = "cached";
    // Corrections made before signing in belong to this artist too.
    carried = sessionDictionary_.entries();
    sessionDictionary_.clear();
    saveAccountFileLocked();
    active = activeProfileId_;
    if (active.empty()) {
      message_ = profiles_.empty()
                     ? "Signed in, but no seat is assigned to this email yet."
                     : "Signed in, but every seat for this email is suspended.";
      bumpRevisionLocked();
    }
  }

  if (active.empty())
    return emitStatus();

  doSetActive(active, false);

  if (!carried.empty()) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (isPaidLocked()) {
        for (const auto &entry : carried)
          profileDictionary_.addCorrection(entry.original, entry.corrected,
                                           entry.updatedAt, true);
        saveActiveDictionaryLocked();
        bumpRevisionLocked();
      }
    }
    doFlush();
  }
  emitStatus();
}

void AccountManager::doLogout(bool tellServer) {
  std::string token;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    token = session_.token;
  }
  if (tellServer && !token.empty() && cloudAvailable())
    cloud_->logout(token); // best effort; local state is cleared regardless

  std::lock_guard<std::mutex> lock(mutex_);
  clearLocalPaidStateLocked(true);
  removeAccountFileLocked();
  sessionDictionary_.clear();
  sync_ = "local";
  login_ = LoginState{};
  message_ = token.empty() ? std::string{}
                           : "Signed out. Corrections now stay in this session.";
  bumpRevisionLocked();
  emitStatusLocked();
}

void AccountManager::doSetActive(const std::string &profileId, bool announce) {
  std::string token;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const CloudProfile *profile = findProfileLocked(profileId);
    if (profile == nullptr) {
      message_ = "That profile is not available for this sign-in.";
      return emitStatusLocked();
    }
    activeProfileId_ = profileId;
    message_.clear();
    saveAccountFileLocked();
    loadActiveDictionaryLocked();
    sync_ = "cached";
    bumpRevisionLocked();
    token = session_.token;
  }
  if (announce)
    emitStatus();
  if (!cloudAvailable() || token.empty())
    return;

  const DictionaryResult result = cloud_->fetchDictionary(token, profileId);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (activeProfileId_ != profileId)
      return; // switched again while we were fetching
    if (result.ok) {
      profileDictionary_.replaceFromCloud(result.entries, result.version);
      saveActiveDictionaryLocked();
      sync_ = "synced";
      message_.clear();
      bumpRevisionLocked();
    } else if (result.unauthorized()) {
      handleUnauthorizedLocked("Session expired. Sign in again.");
      return emitStatusLocked();
    } else if (result.networkError) {
      sync_ = "offline";
    } else if (result.error == "suspended") {
      sync_ = "error";
      message_ = "This workspace is suspended.";
      for (auto &p : profiles_)
        if (p.id == profileId)
          p.status = "suspended";
      bumpRevisionLocked();
    } else {
      sync_ = "error";
      message_ = transportMessage(result, "Could not load the dictionary.");
    }
  }
  doFlush();
  emitStatus();
}

void AccountManager::doRefresh() {
  std::string token;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    token = session_.token;
  }
  if (token.empty() || !cloudAvailable())
    return;

  const MeResult result = cloud_->me(token);
  std::string active;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (result.ok) {
      if (!result.email.empty())
        session_.email = result.email;
      adoptProfilesLocked(result.profiles);
      saveAccountFileLocked();
      active = activeProfileId_;
      if (active.empty()) {
        sync_ = "synced";
        message_ = profiles_.empty()
                       ? "No seat is assigned to this email yet."
                       : "Every seat for this email is suspended.";
        bumpRevisionLocked();
        return emitStatusLocked();
      }
    } else if (result.unauthorized()) {
      handleUnauthorizedLocked("Session expired. Sign in again.");
      return emitStatusLocked();
    } else if (result.networkError) {
      sync_ = "offline";
      return emitStatusLocked();
    } else {
      sync_ = "error";
      message_ = transportMessage(result, "Could not refresh the profile.");
      return emitStatusLocked();
    }
  }
  doSetActive(active, false);
}

void AccountManager::doFlush() {
  if (!cloudAvailable())
    return;
  std::string token;
  std::string profileId;
  std::vector<Dictionary::Entry> pending;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isPaidLocked())
      return;
    token = session_.token;
    profileId = activeProfileId_;
    pending = profileDictionary_.pendingEntries();
  }
  if (pending.empty())
    return;

  bool changed = false;
  for (const auto &entry : pending) {
    const PushResult result =
        cloud_->pushCorrection(token, profileId, entry.original, entry.corrected);
    std::lock_guard<std::mutex> lock(mutex_);
    if (activeProfileId_ != profileId || session_.token != token)
      return;
    if (result.ok) {
      profileDictionary_.markSynced(entry.original);
      profileDictionary_.setVersion(result.version);
      sync_ = "synced";
      changed = true;
    } else if (result.unauthorized()) {
      handleUnauthorizedLocked("Session expired. Sign in again.");
      return emitStatusLocked();
    } else if (result.networkError) {
      sync_ = "offline";
      break;
    } else if (result.error == "invalid_term") {
      profileDictionary_.markSynced(entry.original); // server will never take it
      changed = true;
    } else {
      sync_ = "error";
      message_ = transportMessage(result, "Could not sync a correction.");
      break;
    }
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (changed)
      saveActiveDictionaryLocked();
  }
  emitStatus();
}

// ── locked helpers ───────────────────────────────────────────────────────

bool AccountManager::cloudAvailable() const {
  return cloud_ != nullptr && !config_.profileApiUrl.empty();
}

bool AccountManager::isPaidLocked() const {
  if (session_.token.empty() || activeProfileId_.empty())
    return false;
  const CloudProfile *profile = findProfileLocked(activeProfileId_);
  return profile != nullptr && profile->status == "active";
}

const CloudProfile *
AccountManager::findProfileLocked(const std::string &id) const {
  for (const auto &p : profiles_)
    if (p.id == id)
      return &p;
  return nullptr;
}

std::string
AccountManager::profileCachePath(const std::string &profileId) const {
  return config_.dataDir + "/profiles/" + safeFileComponent(profileId) +
         ".json";
}

void AccountManager::loadActiveDictionaryLocked() {
  profileDictionary_.clear();
  if (activeProfileId_.empty())
    return;
  profileDictionary_.loadFromFile(profileCachePath(activeProfileId_));
}

void AccountManager::saveActiveDictionaryLocked() const {
  if (activeProfileId_.empty())
    return;
  const std::string path = profileCachePath(activeProfileId_);
  if (profileDictionary_.saveToFile(path))
    restrictToOwner(path);
}

void AccountManager::adoptProfilesLocked(std::vector<CloudProfile> profiles) {
  profiles_ = std::move(profiles);
  if (findProfileLocked(activeProfileId_) != nullptr)
    return;
  activeProfileId_.clear();
  for (const auto &p : profiles_) {
    if (p.status == "active") {
      activeProfileId_ = p.id;
      return;
    }
  }
  if (!profiles_.empty())
    activeProfileId_ = profiles_.front().id;
}

void AccountManager::handleUnauthorizedLocked(const std::string &reason) {
  clearLocalPaidStateLocked(true);
  removeAccountFileLocked();
  sync_ = "local";
  message_ = reason;
  bumpRevisionLocked();
}

void AccountManager::bumpRevisionLocked() { revision_.fetch_add(1); }

void AccountManager::clearLocalPaidStateLocked(bool deleteCaches) {
  if (deleteCaches) {
    std::error_code ec;
    for (const auto &p : profiles_)
      std::filesystem::remove(profileCachePath(p.id), ec);
    if (!activeProfileId_.empty())
      std::filesystem::remove(profileCachePath(activeProfileId_), ec);
  }
  session_ = Session{};
  profiles_.clear();
  activeProfileId_.clear();
  profileDictionary_.clear();
}

// ── persistence ──────────────────────────────────────────────────────────

bool AccountManager::loadAccountFile() {
  const std::string path = config_.dataDir + "/" + kAccountFile;
  std::ifstream in(path);
  if (!in.is_open())
    return false;
  json doc;
  try {
    in >> doc;
  } catch (const std::exception &e) {
    std::cerr << "[AccountManager] cannot parse " << path << ": " << e.what()
              << std::endl;
    return false;
  }
  if (!doc.is_object())
    return false;

  // A token minted by a different deployment is useless here.
  if (doc.value("profileApiUrl", std::string{}) != config_.profileApiUrl)
    return false;

  std::lock_guard<std::mutex> lock(mutex_);
  const auto sessionIt = doc.find("session");
  if (sessionIt != doc.end() && sessionIt->is_object()) {
    session_.token = sessionIt->value("token", std::string{});
    session_.userId = sessionIt->value("userId", std::string{});
    session_.email = sessionIt->value("email", std::string{});
  }
  std::vector<CloudProfile> profiles;
  const auto profilesIt = doc.find("profiles");
  if (profilesIt != doc.end() && profilesIt->is_array()) {
    for (const auto &item : *profilesIt) {
      CloudProfile p = profileFromJson(item);
      if (!p.id.empty())
        profiles.push_back(std::move(p));
    }
  }
  activeProfileId_ = doc.value("activeProfileId", std::string{});
  adoptProfilesLocked(std::move(profiles));
  return !session_.token.empty();
}

void AccountManager::saveAccountFileLocked() const {
  json doc;
  doc["profileApiUrl"] = config_.profileApiUrl;
  doc["session"] = {{"token", session_.token},
                    {"userId", session_.userId},
                    {"email", session_.email}};
  doc["activeProfileId"] = activeProfileId_;
  json profiles = json::array();
  for (const auto &p : profiles_)
    profiles.push_back(profileToJson(p));
  doc["profiles"] = std::move(profiles);

  std::error_code ec;
  std::filesystem::create_directories(config_.dataDir, ec);
  const std::string path = config_.dataDir + "/" + kAccountFile;
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out.is_open()) {
      std::cerr << "[AccountManager] cannot write " << tmp << std::endl;
      return;
    }
    out << doc.dump(2) << std::endl;
  }
  restrictToOwner(tmp);
  std::filesystem::rename(tmp, path, ec);
  if (ec)
    std::cerr << "[AccountManager] cannot move " << tmp << ": " << ec.message()
              << std::endl;
  restrictToOwner(path);
}

void AccountManager::removeAccountFileLocked() const {
  std::error_code ec;
  std::filesystem::remove(config_.dataDir + "/" + kAccountFile, ec);
}

// ── status ───────────────────────────────────────────────────────────────

void AccountManager::emitStatus() {
  std::lock_guard<std::mutex> lock(mutex_);
  emitStatusLocked();
}

void AccountManager::emitStatusLocked() {
  const bool paid = isPaidLocked();
  const std::string tierNow = paid ? "paid" : "free";
  if (tierNow != lastLoggedTier_) {
    lastLoggedTier_ = tierNow;
    if (paid) {
      const CloudProfile *p = findProfileLocked(activeProfileId_);
      std::cout << "Profile: paid — " << (p ? p->name : activeProfileId_)
                << (p && !p->workspaceName.empty() ? " (" + p->workspaceName + ")"
                                                    : "")
                << std::endl;
    } else {
      std::cout << "Profile: free — local, session-only corrections"
                << std::endl;
    }
  }
  if (!statusSink_)
    return;
  // Copy the sink so a re-entrant setStatusSink cannot invalidate it.
  const auto sink = statusSink_;
  sink(statusJsonLocked());
}

std::string AccountManager::statusJsonLocked() const {
  const bool paid = isPaidLocked();
  const Dictionary &dict = paid ? profileDictionary_ : sessionDictionary_;

  json out;
  out["type"] = "profileStatus";
  out["tier"] = paid ? "paid" : "free";
  out["cloudAvailable"] = cloudAvailable();
  out["signedIn"] = !session_.token.empty();
  out["email"] = session_.email;

  const CloudProfile *active = findProfileLocked(activeProfileId_);
  out["activeProfile"] = active ? profileToJson(*active) : json(nullptr);

  json profiles = json::array();
  for (const auto &p : profiles_)
    profiles.push_back(profileToJson(p));
  out["profiles"] = std::move(profiles);

  json dictionary;
  dictionary["scope"] = paid ? "profile" : "session";
  dictionary["entries"] = dict.entryCount();
  dictionary["biasTerms"] = dict.vocabularyForBias().size();
  dictionary["pending"] = dict.pendingEntries().size();
  dictionary["version"] = dict.version();
  out["dictionary"] = std::move(dictionary);

  out["sync"] = paid ? sync_ : "local";

  json login;
  login["stage"] = login_.stage;
  login["email"] = login_.email;
  login["echoedCode"] = login_.echoedCode;
  login["message"] = login_.message;
  out["login"] = std::move(login);

  out["message"] = message_;
  return out.dump();
}

} // namespace punch2pen
