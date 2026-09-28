#include "CloudProfileClient.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace punch2pen {

namespace {

using json = nlohmann::json;

std::string trimTrailingSlash(std::string url) {
  while (!url.empty() && url.back() == '/')
    url.pop_back();
  return url;
}

// Fills the CloudResult fields shared by every endpoint and returns the
// parsed body when the response is usable JSON.
json applyEnvelope(CloudResult &result, const HttpResponse &response) {
  result.httpStatus = response.status;
  if (!response.error.empty() && response.status == 0) {
    result.ok = false;
    result.networkError = true;
    result.error = "network";
    result.message = response.error;
    return json();
  }
  json body;
  try {
    body = response.body.empty() ? json::object() : json::parse(response.body);
  } catch (const std::exception &) {
    body = json::object();
  }
  if (!body.is_object())
    body = json::object();

  result.ok = body.value("ok", false) && response.status >= 200 &&
              response.status < 300;
  if (!result.ok) {
    result.error = body.value("error", std::string{});
    if (result.error.empty())
      result.error = "http_" + std::to_string(response.status);
    result.message = body.value("message", std::string{});
    if (result.message.empty())
      result.message = "Profile API returned HTTP " +
                       std::to_string(response.status);
  }
  return body;
}

std::vector<CloudProfile> parseProfiles(const json &body) {
  std::vector<CloudProfile> out;
  const auto it = body.find("profiles");
  if (it == body.end() || !it->is_array())
    return out;
  for (const auto &item : *it) {
    if (!item.is_object())
      continue;
    CloudProfile profile;
    profile.id = item.value("id", std::string{});
    profile.name = item.value("name", std::string{});
    profile.workspaceId = item.value("workspaceId", std::string{});
    profile.workspaceName = item.value("workspaceName", std::string{});
    profile.role = item.value("role", std::string{"artist"});
    profile.status = item.value("status", std::string{"active"});
    profile.dictionaryVersion = item.value("dictionaryVersion", int64_t{0});
    if (!profile.id.empty())
      out.push_back(std::move(profile));
  }
  return out;
}

std::string userId(const json &body) {
  const auto it = body.find("user");
  if (it == body.end() || !it->is_object())
    return {};
  return it->value("id", std::string{});
}

std::string userEmail(const json &body) {
  const auto it = body.find("user");
  if (it == body.end() || !it->is_object())
    return {};
  return it->value("email", std::string{});
}

} // namespace

HttpCloudProfileClient::HttpCloudProfileClient(
    std::string baseUrl, std::unique_ptr<HttpTransport> transport)
    : baseUrl_(trimTrailingSlash(std::move(baseUrl))),
      transport_(std::move(transport)) {}

HttpResponse HttpCloudProfileClient::call(const std::string &method,
                                          const std::string &path,
                                          const std::string &token,
                                          const std::string &body) {
  std::map<std::string, std::string> headers;
  headers["Accept"] = "application/json";
  headers["User-Agent"] = "punch2penEngine/1.0";
  if (!body.empty())
    headers["Content-Type"] = "application/json";
  if (!token.empty())
    headers["Authorization"] = "Bearer " + token;
  if (!transport_) {
    HttpResponse none;
    none.error = "no HTTP transport";
    return none;
  }
  return transport_->request(method, baseUrl_ + path, headers, body);
}

LoginStartResult HttpCloudProfileClient::startLogin(const std::string &email) {
  json req;
  req["email"] = email;
  const HttpResponse response = call("POST", "/v1/auth/start", "", req.dump());
  LoginStartResult result;
  const json body = applyEnvelope(result, response);
  if (result.ok) {
    result.delivery = body.value("delivery", std::string{"email"});
    result.echoedCode = body.value("code", std::string{});
  }
  return result;
}

LoginVerifyResult HttpCloudProfileClient::verifyLogin(
    const std::string &email, const std::string &code,
    const std::string &deviceName, const std::string &platform) {
  json req;
  req["email"] = email;
  req["code"] = code;
  req["device"] = {{"name", deviceName}, {"platform", platform}};
  const HttpResponse response =
      call("POST", "/v1/auth/verify", "", req.dump());
  LoginVerifyResult result;
  const json body = applyEnvelope(result, response);
  if (result.ok) {
    result.token = body.value("token", std::string{});
    result.userId = userId(body);
    result.email = userEmail(body);
    result.profiles = parseProfiles(body);
    if (result.token.empty()) {
      result.ok = false;
      result.error = "bad_response";
      result.message = "Profile API returned no session token";
    }
  }
  return result;
}

CloudResult HttpCloudProfileClient::logout(const std::string &token) {
  const HttpResponse response = call("POST", "/v1/auth/logout", token, "{}");
  CloudResult result;
  applyEnvelope(result, response);
  return result;
}

MeResult HttpCloudProfileClient::me(const std::string &token) {
  const HttpResponse response = call("GET", "/v1/me", token, "");
  MeResult result;
  const json body = applyEnvelope(result, response);
  if (result.ok) {
    result.userId = userId(body);
    result.email = userEmail(body);
    result.profiles = parseProfiles(body);
  }
  return result;
}

DictionaryResult
HttpCloudProfileClient::fetchDictionary(const std::string &token,
                                        const std::string &profileId) {
  const HttpResponse response =
      call("GET", "/v1/profiles/" + profileId + "/dictionary", token, "");
  DictionaryResult result;
  const json body = applyEnvelope(result, response);
  if (!result.ok)
    return result;
  result.profileId = body.value("profileId", profileId);
  result.version = body.value("version", int64_t{0});
  const auto it = body.find("entries");
  if (it != body.end() && it->is_array()) {
    for (const auto &item : *it) {
      if (!item.is_object())
        continue;
      Dictionary::Entry entry;
      entry.original = item.value("original", std::string{});
      entry.corrected = item.value("corrected", std::string{});
      entry.count = item.value("count", 1);
      entry.updatedAt = item.value("updatedAt", int64_t{0});
      entry.pendingSync = false;
      if (!entry.original.empty() && !entry.corrected.empty())
        result.entries.push_back(std::move(entry));
    }
  }
  return result;
}

PushResult HttpCloudProfileClient::pushCorrection(const std::string &token,
                                                  const std::string &profileId,
                                                  const std::string &original,
                                                  const std::string &corrected) {
  json req;
  req["original"] = original;
  req["corrected"] = corrected;
  const HttpResponse response =
      call("POST", "/v1/profiles/" + profileId + "/corrections", token,
           req.dump());
  PushResult result;
  const json body = applyEnvelope(result, response);
  if (result.ok) {
    result.version = body.value("version", int64_t{0});
    result.count = body.value("count", 0);
  }
  return result;
}

} // namespace punch2pen
