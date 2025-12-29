#pragma once
#include <string>
#include <vector>

namespace punch2pen {
class ProfileManager {
public:
  ProfileManager() = default;
  ~ProfileManager() = default;

  void loadProfile(const std::string &userId) {
    // TODO: Load from disk
  }

  void saveProfile() {
    // TODO: Save to disk
  }
};
} // namespace punch2pen
