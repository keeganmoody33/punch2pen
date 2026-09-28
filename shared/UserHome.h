#pragma once

#include <cstdlib>
#include <string>

#if !defined(_WIN32)
#include <pwd.h>
#include <unistd.h>
#endif

namespace punch2pen {

// Directory that should hold `.punch2pen/` (model, logs, corrections).
//
// Logic's AU host rewrites HOME to a container. Prefer the passwd home in
// that case so the engine still finds ~/.punch2pen/models/ggml-base.bin.
// Explicit HOME (verify skill, Terminal) is kept when we are not sandboxed.
inline std::string realUserHome() {
  if (const char *overrideHome = std::getenv("PUNCH2PEN_HOME")) {
    if (overrideHome[0] != '\0')
      return overrideHome;
  }

#if !defined(_WIN32)
  const char *pwDir = nullptr;
  if (const passwd *pw = getpwuid(getuid())) {
    if (pw->pw_dir != nullptr && pw->pw_dir[0] != '\0')
      pwDir = pw->pw_dir;
  }
#endif

  const char *envHome = std::getenv("HOME");
#if defined(_WIN32)
  if (envHome == nullptr || envHome[0] == '\0')
    envHome = std::getenv("USERPROFILE");
#endif

  if (std::getenv("APP_SANDBOX_CONTAINER_ID") != nullptr) {
#if !defined(_WIN32)
    if (pwDir != nullptr)
      return pwDir;
#endif
  }

  if (envHome != nullptr && envHome[0] != '\0')
    return envHome;

#if !defined(_WIN32)
  if (pwDir != nullptr)
    return pwDir;
#endif

  return ".";
}

inline std::string punch2penDataDir() {
  return realUserHome() + "/.punch2pen";
}

} // namespace punch2pen
