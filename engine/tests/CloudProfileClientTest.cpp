#include "../src/CloudProfileClient.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

struct Recorded {
  std::string method;
  std::string url;
  std::map<std::string, std::string> headers;
  std::string body;
};

class FakeTransport : public punch2pen::HttpTransport {
public:
  std::vector<Recorded> requests;
  punch2pen::HttpResponse next;

  punch2pen::HttpResponse request(const std::string &method,
                                  const std::string &url,
                                  const std::map<std::string, std::string> &headers,
                                  const std::string &body) override {
    requests.push_back({method, url, headers, body});
    return next;
  }
};

struct Harness {
  FakeTransport *transport = nullptr;
  punch2pen::HttpCloudProfileClient client;

  Harness()
      : client("https://api.example.test/", [this] {
          auto t = std::make_unique<FakeTransport>();
          transport = t.get();
          return t;
        }()) {}

  void respond(int status, const json &body) {
    transport->next.status = status;
    transport->next.body = body.dump();
    transport->next.error.clear();
  }

  const Recorded &last() const { return transport->requests.back(); }
};

void testStartLoginEcho() {
  Harness h;
  h.respond(200, {{"ok", true}, {"delivery", "echo"}, {"code", "123456"}});
  const auto r = h.client.startLogin("A@Example.test");
  assert(r.ok);
  assert(r.delivery == "echo");
  assert(r.echoedCode == "123456");
  assert(h.last().method == "POST");
  assert(h.last().url == "https://api.example.test/v1/auth/start" &&
         "trailing slash on the base URL must not double up");
  assert(h.last().headers.at("Content-Type") == "application/json");
  assert(h.last().headers.count("Authorization") == 0);
  assert(json::parse(h.last().body)["email"] == "A@Example.test");
  std::cout << "[PASS] testStartLoginEcho" << std::endl;
}

void testStartLoginNotInvited() {
  Harness h;
  h.respond(403, {{"ok", false},
                  {"error", "not_invited"},
                  {"message", "No seat for this email yet."}});
  const auto r = h.client.startLogin("nobody@example.test");
  assert(!r.ok);
  assert(r.httpStatus == 403);
  assert(r.error == "not_invited");
  assert(r.message == "No seat for this email yet.");
  assert(!r.networkError);
  std::cout << "[PASS] testStartLoginNotInvited" << std::endl;
}

void testVerifyLoginParsesProfiles() {
  Harness h;
  h.respond(200, {{"ok", true},
                  {"token", "p2p_abc"},
                  {"user", {{"id", "user_1"}, {"email", "a@example.test"}}},
                  {"profiles",
                   json::array({{{"id", "prof_a"},
                                 {"name", "Keegan"},
                                 {"workspaceId", "ws_1"},
                                 {"workspaceName", "Studio X"},
                                 {"role", "owner"},
                                 {"status", "active"},
                                 {"dictionaryVersion", 3}}})}});
  const auto r = h.client.verifyLogin("a@example.test", "123456", "mbp", "macos");
  assert(r.ok);
  assert(r.token == "p2p_abc");
  assert(r.userId == "user_1");
  assert(r.email == "a@example.test");
  assert(r.profiles.size() == 1);
  assert(r.profiles[0].id == "prof_a");
  assert(r.profiles[0].workspaceName == "Studio X");
  assert(r.profiles[0].role == "owner");
  assert(r.profiles[0].dictionaryVersion == 3);
  const json body = json::parse(h.last().body);
  assert(body["code"] == "123456");
  assert(body["device"]["name"] == "mbp");
  assert(body["device"]["platform"] == "macos");
  std::cout << "[PASS] testVerifyLoginParsesProfiles" << std::endl;
}

void testVerifyWithoutTokenIsRejected() {
  Harness h;
  h.respond(200, {{"ok", true}});
  const auto r = h.client.verifyLogin("a@example.test", "123456", "d", "p");
  assert(!r.ok && r.error == "bad_response");
  std::cout << "[PASS] testVerifyWithoutTokenIsRejected" << std::endl;
}

void testBearerTokenAndDictionaryParse() {
  Harness h;
  h.respond(200, {{"ok", true},
                  {"profileId", "prof_a"},
                  {"version", 9},
                  {"entries", json::array({{{"original", "nah"},
                                            {"corrected", "hell nah"},
                                            {"count", 2},
                                            {"updatedAt", 1234}},
                                           {{"original", ""},
                                            {"corrected", "dropped"}}})}});
  const auto r = h.client.fetchDictionary("p2p_abc", "prof_a");
  assert(r.ok);
  assert(r.version == 9);
  assert(r.entries.size() == 1 && "entries with an empty side are dropped");
  assert(r.entries[0].original == "nah");
  assert(r.entries[0].corrected == "hell nah");
  assert(r.entries[0].count == 2);
  assert(!r.entries[0].pendingSync);
  assert(h.last().method == "GET");
  assert(h.last().url == "https://api.example.test/v1/profiles/prof_a/dictionary");
  assert(h.last().headers.at("Authorization") == "Bearer p2p_abc");
  assert(h.last().headers.count("Content-Type") == 0);
  std::cout << "[PASS] testBearerTokenAndDictionaryParse" << std::endl;
}

void testPushCorrection() {
  Harness h;
  h.respond(200, {{"ok", true}, {"version", 10}, {"count", 3}});
  const auto r = h.client.pushCorrection("p2p_abc", "prof_a", "Kegan", "Keegan");
  assert(r.ok && r.version == 10 && r.count == 3);
  assert(h.last().url ==
         "https://api.example.test/v1/profiles/prof_a/corrections");
  const json body = json::parse(h.last().body);
  assert(body["original"] == "Kegan" && body["corrected"] == "Keegan");
  std::cout << "[PASS] testPushCorrection" << std::endl;
}

void testUnauthorizedAndNetworkErrors() {
  Harness h;
  h.respond(401, {{"ok", false}, {"error", "unauthorized"}});
  const auto me = h.client.me("stale");
  assert(!me.ok && me.unauthorized() && me.error == "unauthorized");

  h.transport->next = punch2pen::HttpResponse{};
  h.transport->next.error = "connect timeout";
  const auto down = h.client.me("p2p_abc");
  assert(!down.ok && down.networkError && down.httpStatus == 0);
  assert(down.message == "connect timeout");

  h.respond(500, json::object());
  const auto boom = h.client.logout("p2p_abc");
  assert(!boom.ok && boom.error == "http_500" && !boom.networkError);

  h.transport->next.status = 200;
  h.transport->next.body = "not json";
  const auto garbage = h.client.me("p2p_abc");
  assert(!garbage.ok && "non-JSON body must not count as success");
  std::cout << "[PASS] testUnauthorizedAndNetworkErrors" << std::endl;
}

} // namespace

int main() {
  testStartLoginEcho();
  testStartLoginNotInvited();
  testVerifyLoginParsesProfiles();
  testVerifyWithoutTokenIsRejected();
  testBearerTokenAndDictionaryParse();
  testPushCorrection();
  testUnauthorizedAndNetworkErrors();
  std::cout << "All CloudProfileClient tests passed!" << std::endl;
  return 0;
}
