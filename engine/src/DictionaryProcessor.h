#pragma once

#include "DatabaseManager.h"
#include <string>
#include <vector>

namespace punch2pen {

class DictionaryProcessor {
public:
  DictionaryProcessor(DatabaseManager &db);
  ~DictionaryProcessor();

  // Refresh active vocabulary from database
  void refresh();

  // Get the current prompt string for the transcriber
  std::string getInitialPrompt() const;

private:
  DatabaseManager &database;
  std::vector<std::string> activeVocabulary;
  mutable std::mutex promptLock;
};

} // namespace punch2pen
