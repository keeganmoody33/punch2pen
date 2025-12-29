#include "DictionaryProcessor.h"
#include <numeric>

namespace punch2pen {

DictionaryProcessor::DictionaryProcessor(DatabaseManager &db) : database(db) {
  refresh();
}

DictionaryProcessor::~DictionaryProcessor() {}

void DictionaryProcessor::refresh() {
  std::lock_guard<std::mutex> lock(promptLock);
  activeVocabulary = database.getVocabulary();
}

std::string DictionaryProcessor::getInitialPrompt() const {
  std::lock_guard<std::mutex> lock(promptLock);
  if (activeVocabulary.empty())
    return "";

  // Join words with spaces or commas
  std::string prompt = "Context: ";
  for (size_t i = 0; i < activeVocabulary.size(); ++i) {
    prompt += activeVocabulary[i];
    if (i < activeVocabulary.size() - 1)
      prompt += ", ";
  }
  return prompt;
}

} // namespace punch2pen
