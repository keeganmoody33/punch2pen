#pragma once

#include "CloudProfileClient.h"

namespace punch2pen {

// HTTPS transport over the IXWebSocket HTTP client the engine already links
// for OpenAI Realtime. Blocking; only ever called from AccountManager's
// worker thread, never from the audio/coordinator loop.
class IxHttpTransport : public HttpTransport {
public:
  explicit IxHttpTransport(int timeoutSeconds = 20);

  HttpResponse request(const std::string &method, const std::string &url,
                       const std::map<std::string, std::string> &headers,
                       const std::string &body) override;

private:
  int timeoutSeconds_;
};

} // namespace punch2pen
