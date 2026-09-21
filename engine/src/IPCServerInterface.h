#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace punch2pen {

class IPCServerInterface {
public:
  virtual ~IPCServerInterface() = default;

  virtual bool hasPendingAudio() = 0;
  virtual std::vector<float> popAudio() = 0;
  virtual double lastAudioDawSampleTime() = 0;
  virtual double lastAudioSampleRate() = 0;
  virtual uint32_t lastAudioCaptureEpoch() = 0;
  virtual bool transportStateChangedToStop() = 0;

  struct CorrectionPair {
    std::string original;
    std::string corrected;
  };

  virtual bool hasPendingCorrection() = 0;
  virtual CorrectionPair popCorrection() = 0;

  // JSON ProfileCommand payloads from plugins (see shared/Protocol.h).
  virtual bool hasPendingProfileCommand() = 0;
  virtual std::string popProfileCommand() = 0;
};

} // namespace punch2pen

namespace Punch2Pen = punch2pen;
