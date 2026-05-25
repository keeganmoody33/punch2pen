#pragma once

#include <string>
#include <vector>

namespace punch2pen {

class TranscriberInterface {
public:
  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void onTranscriptUpdated(const std::string &text,
                                     bool isProvisional) = 0;
  };

  virtual ~TranscriberInterface() = default;

  virtual void pushAudioBlock(const float *samples, int sampleCount,
                              double dawSampleTime) = 0;
  virtual void addListener(Listener *listener) = 0;
  virtual void removeListener(Listener *listener) = 0;
  virtual void setVocabularyBias(const std::vector<std::string> &words) = 0;
  virtual void finalizeStream() = 0;
};

} // namespace punch2pen

namespace Punch2Pen = punch2pen;
