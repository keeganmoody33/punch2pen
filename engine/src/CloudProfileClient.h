#pragma once

#include "Dictionary.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace punch2pen {

// One seat the signed-in user can act as. Mirrors profileSummaryValidator in
// cloud/convex/profiles.ts.
struct CloudProfile {
  std::string id;
  std::string name;
  std::string workspaceId;
  std::string workspaceName;
  std::string role;   // "owner" | "artist"
  std::string status; // "active" | "suspended"
  int64_t dictionaryVersion = 0;
};

struct CloudResult {
  bool ok = false;
  int httpStatus = 0;
  bool networkError = false; // no HTTP round trip completed
  std::string error;         // machine code from the API, e.g. "not_invited"
  std::string message;       // human copy from the API or transport
  bool unauthorized() const { return httpStatus == 401; }
};

struct LoginStartResult : CloudResult {
  std::string delivery;   // "email" | "echo"
  std::string echoedCode; // only when the deployment runs in echo mode
};

struct LoginVerifyResult : CloudResult {
  std::string token;
  std::string userId;
  std::string email;
  std::vector<CloudProfile> profiles;
};

struct MeResult : CloudResult {
  std::string userId;
  std::string email;
  std::vector<CloudProfile> profiles;
};

struct DictionaryResult : CloudResult {
  std::string profileId;
  int64_t version = 0;
  std::vector<Dictionary::Entry> entries;
};

struct PushResult : CloudResult {
  int64_t version = 0;
  int count = 0;
};

// Everything AccountManager needs from the profile API. Implemented over
// HTTP for the engine; mocked in tests. Nothing here knows about billing.
class CloudProfileClient {
public:
  virtual ~CloudProfileClient() = default;

  virtual LoginStartResult startLogin(const std::string &email) = 0;
  virtual LoginVerifyResult verifyLogin(const std::string &email,
                                        const std::string &code,
                                        const std::string &deviceName,
                                        const std::string &platform) = 0;
  virtual CloudResult logout(const std::string &token) = 0;
  virtual MeResult me(const std::string &token) = 0;
  virtual DictionaryResult fetchDictionary(const std::string &token,
                                           const std::string &profileId) = 0;
  virtual PushResult pushCorrection(const std::string &token,
                                    const std::string &profileId,
                                    const std::string &original,
                                    const std::string &corrected) = 0;
};

// Minimal HTTP seam so request shaping and JSON parsing are testable
// without a network. The engine plugs IXWebSocket in (IxHttpTransport).
struct HttpResponse {
  int status = 0;
  std::string body;
  std::string error; // non-empty when no response was received
};

class HttpTransport {
public:
  virtual ~HttpTransport() = default;
  virtual HttpResponse request(const std::string &method, const std::string &url,
                               const std::map<std::string, std::string> &headers,
                               const std::string &body) = 0;
};

class HttpCloudProfileClient : public CloudProfileClient {
public:
  HttpCloudProfileClient(std::string baseUrl,
                         std::unique_ptr<HttpTransport> transport);

  LoginStartResult startLogin(const std::string &email) override;
  LoginVerifyResult verifyLogin(const std::string &email,
                                const std::string &code,
                                const std::string &deviceName,
                                const std::string &platform) override;
  CloudResult logout(const std::string &token) override;
  MeResult me(const std::string &token) override;
  DictionaryResult fetchDictionary(const std::string &token,
                                   const std::string &profileId) override;
  PushResult pushCorrection(const std::string &token,
                            const std::string &profileId,
                            const std::string &original,
                            const std::string &corrected) override;

  const std::string &baseUrl() const { return baseUrl_; }

private:
  HttpResponse call(const std::string &method, const std::string &path,
                    const std::string &token, const std::string &body);

  std::string baseUrl_;
  std::unique_ptr<HttpTransport> transport_;
};

} // namespace punch2pen
