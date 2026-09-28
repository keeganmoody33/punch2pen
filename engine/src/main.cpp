#include "AccountManager.h"
#include "CloudProfileClient.h"
#include "IPCServer.h"
#include "IxHttpTransport.h"
#include "OpenAICloudTranscriber.h"
#include "Transcriber.h"
#include "TranscriberInterface.h"
#include "TranscriptionCoordinator.h"
#include "UserHome.h"

#include <nlohmann/json.hpp>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifndef PUNCH2PEN_PROFILE_API_URL_DEFAULT
#define PUNCH2PEN_PROFILE_API_URL_DEFAULT ""
#endif

volatile std::sig_atomic_t g_shutdownRequested = 0;
static punch2pen::TranscriptionCoordinator *g_coordinator = nullptr;

void signalHandler(int signum) {
  (void)signum;
  g_shutdownRequested = 1;
  if (g_coordinator != nullptr) {
    g_coordinator->stop();
  }
}

class EngineTranscriberListener
    : public punch2pen::TranscriberInterface::Listener {
public:
  EngineTranscriberListener(punch2pen::IPCServer &serverRef,
                            punch2pen::AccountManager &accountRef)
      : server(serverRef), account(accountRef) {}

  void onTranscriptUpdated(const std::string &text, bool isProvisional,
                           double startTime, double endTime,
                           uint32_t captureEpoch) override {
    (void)isProvisional;
    // Case-sensitive dictionary map on the raw word, before anything
    // downstream summarises or displays it.
    const std::string mapped = account.mapWord(text);
    std::cout << "Transcription: " << mapped << std::endl;
    server.sendResult(mapped, startTime, endTime, captureEpoch);
  }

private:
  punch2pen::IPCServer &server;
  punch2pen::AccountManager &account;
};

// Where the paid profile API lives. Free/lite never contacts it; with no URL
// the plugin's sign-in is unavailable and everything stays local.
//   1. PUNCH2PEN_PROFILE_API env (Terminal, verify skill, CI)
//   2. <dataDir>/profile-api.json {"url": "..."} (Mac install without rebuild)
//   3. -DPUNCH2PEN_PROFILE_API_URL compiled default (release builds)
static std::string resolveProfileApiUrl(const std::string &dataDir) {
  if (const char *env = std::getenv("PUNCH2PEN_PROFILE_API")) {
    if (env[0] != '\0')
      return env;
  }
  std::ifstream in(dataDir + "/profile-api.json");
  if (in.is_open()) {
    try {
      nlohmann::json doc;
      in >> doc;
      if (doc.is_object()) {
        const std::string url = doc.value("url", std::string{});
        if (!url.empty())
          return url;
      }
    } catch (const std::exception &e) {
      std::cerr << "Ignoring malformed profile-api.json: " << e.what()
                << std::endl;
    }
  }
  return PUNCH2PEN_PROFILE_API_URL_DEFAULT;
}

static std::string hostName() {
  char buf[256] = {0};
  if (gethostname(buf, sizeof(buf) - 1) == 0 && buf[0] != '\0')
    return buf;
  return "punch2pen engine";
}

static const char *platformName() {
#if defined(__APPLE__)
  return "macos";
#elif defined(_WIN32)
  return "windows";
#else
  return "linux";
#endif
}

static bool fdIsDevNull(int fd) {
  struct stat fdStat {};
  struct stat nullStat {};
  if (fstat(fd, &fdStat) != 0 || stat("/dev/null", &nullStat) != 0)
    return false;
  return S_ISCHR(fdStat.st_mode) && fdStat.st_rdev == nullStat.st_rdev;
}

static void maybeRedirectLogs(const std::string &dataDir) {
  // Launch Services and discarded posix_spawn output attach /dev/null.
  // Shell redirection, verify-skill capture, and CI logs must keep the
  // caller's streams so readiness greps still see "Engine ready."
  if (!fdIsDevNull(STDOUT_FILENO))
    return;
  const std::string logPath = dataDir + "/engine.log";
  const int fd =
      ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0)
    return;
  dup2(fd, STDOUT_FILENO);
  dup2(fd, STDERR_FILENO);
  if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
    close(fd);
}

int main(int argc, char *argv[]) {
  std::signal(SIGPIPE, SIG_IGN);

  bool useCloudMode = false;
  std::string apiKey;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--cloud") {
      useCloudMode = true;
    } else if (arg.rfind("--api-key=", 0) == 0) {
      apiKey = arg.substr(10);
    }
  }

  const std::string dataDir = punch2pen::punch2penDataDir();
  std::filesystem::create_directories(dataDir);
  maybeRedirectLogs(dataDir);

  std::cout << "punch2pen Engine v1.0.0" << std::endl;
  std::cout << "Data dir: " << dataDir << std::endl;

  if (useCloudMode) {
    if (apiKey.empty()) {
      const char *envKey = std::getenv("OPENAI_API_KEY");
      if (envKey != nullptr) {
        apiKey = envKey;
      }
    }
    if (apiKey.empty()) {
      std::cerr << "Cloud mode requested but no API key supplied via "
                   "--api-key= or OPENAI_API_KEY"
                << std::endl;
      return 1;
    }
  }

  punch2pen::IPCServer server(7483);
  // Bind before account/whisper so a freshly launched helper can complete
  // Handshake while those open. Logic otherwise sits on WAIT.
  // Audio queued during that window is consumed after Transcriber is ready.
  if (!server.start()) {
    std::cerr << "Engine cannot listen on 127.0.0.1:7483" << std::endl;
    return 1;
  }

  punch2pen::AccountConfig accountConfig;
  accountConfig.dataDir = dataDir;
  accountConfig.profileApiUrl = resolveProfileApiUrl(dataDir);
  accountConfig.deviceName = hostName();
  accountConfig.platform = platformName();
  std::unique_ptr<punch2pen::CloudProfileClient> profileApi;
  if (!accountConfig.profileApiUrl.empty()) {
    profileApi = std::make_unique<punch2pen::HttpCloudProfileClient>(
        accountConfig.profileApiUrl,
        std::make_unique<punch2pen::IxHttpTransport>());
    std::cout << "Profile API: " << accountConfig.profileApiUrl << std::endl;
  } else {
    std::cout << "Profile API: none (free/lite only, no cloud)" << std::endl;
  }
  punch2pen::AccountManager account(accountConfig, std::move(profileApi));

  punch2pen::TranscriberInterface *activeTranscriber = nullptr;
  std::unique_ptr<punch2pen::TranscriberInterface> cloudTranscriber;
  std::unique_ptr<punch2pen::Transcriber> localTranscriber;

  if (useCloudMode) {
    std::cout << "Mode: [ONLINE] OpenAI Realtime" << std::endl;
    cloudTranscriber =
        std::make_unique<punch2pen::OpenAICloudTranscriber>(apiKey);
    activeTranscriber = cloudTranscriber.get();
  } else {
    std::cout << "Mode: [LOCAL] whisper.cpp" << std::endl;
    const std::string modelPath = dataDir + "/models/ggml-base.bin";
    localTranscriber = std::make_unique<punch2pen::Transcriber>(modelPath);
    if (!localTranscriber->isReady()) {
      std::cerr << "Failed to load whisper model at " << modelPath
                << std::endl;
      return 1;
    }
    activeTranscriber = localTranscriber.get();
  }

  account.setStatusSink(
      [&server](const std::string &json) { server.sendProfileStatus(json); });
  // Loads any saved session + cached dictionary; the coordinator applies the
  // vocabulary bias on its first pass via dictionaryRevision().
  account.start();

  EngineTranscriberListener transcriberListener(server, account);
  activeTranscriber->addListener(&transcriberListener);

  std::cout << "Engine ready." << std::endl;

  punch2pen::TranscriptionCoordinator coordinator(server, *activeTranscriber,
                                                  account);
  g_coordinator = &coordinator;

  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);

  coordinator.run();

  account.stop();
  server.stop();

  return 0;
}
