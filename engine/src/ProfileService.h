#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace punch2pen {

// What TranscriptionCoordinator needs from the account layer. Every call is
// cheap and non-blocking; anything that touches the network happens on
// AccountManager's own worker thread.
class ProfileService {
public:
  virtual ~ProfileService() = default;

  // Correction from the plugin. Free tier: session dictionary. Paid tier:
  // the active profile's dictionary, cached locally and queued for sync.
  virtual void recordCorrection(const std::string &original,
                                const std::string &corrected) = 0;

  // Terms for TranscriberInterface::setVocabularyBias.
  virtual std::vector<std::string> vocabularyForBias() const = 0;

  // Increments whenever vocabularyForBias() would return something new
  // (correction, profile switch, cloud refresh). The coordinator reapplies
  // the bias when it changes.
  virtual uint64_t dictionaryRevision() const = 0;

  // JSON ProfileCommand from the plugin; queued, never processed inline.
  virtual void postCommand(const std::string &json) = 0;
};

} // namespace punch2pen
