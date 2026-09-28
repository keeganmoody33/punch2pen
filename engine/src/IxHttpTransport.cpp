#include "IxHttpTransport.h"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>

namespace punch2pen {

IxHttpTransport::IxHttpTransport(int timeoutSeconds)
    : timeoutSeconds_(timeoutSeconds) {
  ix::initNetSystem();
}

HttpResponse IxHttpTransport::request(
    const std::string &method, const std::string &url,
    const std::map<std::string, std::string> &headers,
    const std::string &body) {
  ix::HttpClient client;
  ix::HttpRequestArgsPtr args = client.createRequest();
  args->connectTimeout = timeoutSeconds_;
  args->transferTimeout = timeoutSeconds_;
  args->followRedirects = false;
  args->maxRedirects = 0;
  args->verbose = false;
  args->compress = false;
  for (const auto &[key, value] : headers)
    args->extraHeaders[key] = value;

  ix::HttpResponsePtr response;
  if (method == "GET")
    response = client.get(url, args);
  else if (method == "POST")
    response = client.post(url, body, args);
  else
    response = client.request(url, method, body, args);

  HttpResponse out;
  if (!response) {
    out.error = "no response";
    return out;
  }
  out.status = response->statusCode;
  out.body = response->body;
  if (response->errorCode != ix::HttpErrorCode::Ok && response->statusCode == 0)
    out.error = response->errorMsg.empty() ? "request failed"
                                           : response->errorMsg;
  return out;
}

} // namespace punch2pen
