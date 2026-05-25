#pragma once

#include <vector>

namespace punch2pen {

class IPCServerInterface {
public:
  virtual ~IPCServerInterface() = default;

  virtual bool hasPendingAudio() = 0;
  virtual std::vector<float> popAudio() = 0;
  virtual bool transportStateChangedToStop() = 0;
};

} // namespace punch2pen

namespace Punch2Pen = punch2pen;
