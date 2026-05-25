#pragma once

#include <string>

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

  virtual void setListener(Listener *listener) = 0;
};

} // namespace punch2pen
