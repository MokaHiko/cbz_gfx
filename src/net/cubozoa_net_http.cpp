#include "cubozoa/net/cubozoa_net_http.h"
#include "asio/buffer.hpp"
#include "spdlog/spdlog.h"

#ifdef WEBGPU_BACKEND_WGPU
#include <asio.hpp>
#endif

#ifdef WEBGPU_BACKEND_EMISCRIPTEN
#include <emscripten/fetch.h>
#endif

#include <nlohmann/json.hpp>

namespace cbz::net {

class ContentType {
public:
  enum Type { JSON, Binary, FormURLEncoded, TextPlain };

  ContentType(Type t) : type(t) {}

  std::string to_string() const { return "Content-Type: " + mime_string(); }

  std::string mime_string() const {
    switch (type) {
    case JSON:
      return "application/json";
    case Binary:
      return "application/octet-stream";
    case FormURLEncoded:
      return "application/x-www-form-urlencoded";
    case TextPlain:
      return "text/plain";
    default:
      return "application/octet-stream";
    }
  }

private:
  Type type;
};

HttpResponse::HttpResponse(HttpResult result, HttpContentType type,
                           Scope<Buffer> &&content)
    : mResult(result), mType(type), mContent(std::move(content)) {}

const char *HttpResponse::readAsCString() const {
  if (static_cast<uint32_t>(mResult) >= 400) {
    spdlog::error("Attempting to parse http response of invalid!");
    return nullptr;
  }

  if (!mContent) {
    return nullptr;
  }

  // if (mContent->getData()[mContent->getSize() - 1] != '\0') {
  //   spdlog::warn("Response content is not a null terminated string!");
  // }
  if (mType != HttpContentType::eApplicationJson) {
    spdlog::warn("Response content type is not of string!");
  }

  return reinterpret_cast<const char *>(mContent->getData());
}

IHttpClient::IHttpClient(const Endpoint &endpoint) : mBaseAddress(endpoint) {};

#ifdef WEBGPU_BACKEND_WGPU
class HttpClientNative : public IHttpClient {
public:
  HttpClientNative(const Endpoint &baseAddress);
  ~HttpClientNative();

  [[nodiscard]] HttpResponse get(const char *path) override;

  [[nodiscard]] HttpResponse postJson(const char *path,
                                      const char *jsonString) override;

protected:
  [[nodiscard]] HttpResponse
  sendRawRequest(const std::string &requestStr) override;

private:
  asio::io_context mIo;
  asio::ip::tcp::socket mSocket;
  asio::ip::tcp::resolver mResolver;
};

[[nodiscard]] std::unique_ptr<IHttpClient>
httpClientCreate(const Endpoint &baseAddress) {
  return std::make_unique<HttpClientNative>(baseAddress);
}

HttpClientNative::HttpClientNative(const Endpoint &endpoint)
    : IHttpClient(endpoint), mSocket(mIo), mResolver(mIo) {
  asio::error_code e;
  asio::ip::tcp::resolver::results_type endPoints =
      mResolver.resolve(endpoint.address.c_str(), endpoint.port.c_str(), e);

  if (e) {
    spdlog::error("Failed to create http cient!{}");
    spdlog::error("{}", e.message());
  }

  asio::connect(mSocket, endPoints, e);
}

HttpClientNative::~HttpClientNative() {
  if (mSocket.is_open()) {
    mSocket.close();
  }
}

[[nodiscard]] HttpResponse HttpClientNative::get(const char *path) {
  std::stringstream requestStream;
  requestStream << "GET " << path << " HTTP/1.1\r\n";
  requestStream << "Host: " << getBaseAddress().address.c_str() << "\r\n";
  requestStream << "Connection: close\r\n";
  requestStream << "\r\n"; // End of headers

  return sendRawRequest(requestStream.str());
}

[[nodiscard]] HttpResponse HttpClientNative::postJson(const char *path,
                                                      const char *jsonString) {
  nlohmann::json json = nlohmann::json::parse(jsonString);
  if (json.is_discarded()) {
    spdlog::error("Could not parse string into json!");
    return {HttpResult::eInvalidJsonData};
  }

  const std::string &bodyString = json.dump();

  std::error_code e;
  std::stringstream requestStream;
  requestStream << "POST " << path << " HTTP/1.1\r\n";
  requestStream << "Host: " << getBaseAddress().address.c_str() << "\r\n";
  requestStream << "Content-Type: application/json" << "\r\n";
  requestStream << "Content-Length: " << bodyString.size() << "\r\n";
  requestStream << "\r\n"; // End of headers
  requestStream << bodyString;

  return sendRawRequest(requestStream.str());
}

[[nodiscard]] HttpResponse
HttpClientNative::sendRawRequest(const std::string &requestStr) {
  std::error_code e;
  asio::write(mSocket, asio::buffer(requestStr), e);

  asio::streambuf responseBuf;

  asio::read_until(mSocket, responseBuf, "\r\n\r\n", e);
  size_t contentSize = 0;
  if (e && e != asio::error::eof) {
    spdlog::error("Read error: {}", e.message());
    return {HttpResult::eNotFound};
  } else {
    std::istream responseStream(&responseBuf);
    std::string headerLine;

    while (std::getline(responseStream, headerLine) && headerLine != "\r") {
      spdlog::info(headerLine);
      if (headerLine.rfind("Content-Length:", 0) == 0) {
        std::string val = headerLine.substr(15);
        contentSize = std::stoul(val);
      }

      if (headerLine.rfind("Content-Type:", 0) == 0) {
        std::string val = headerLine.substr(13);
      }
    }
  }

  uint32_t readAhead = responseBuf.size();
  uint32_t bytesRemaining = contentSize - readAhead;
  if (bytesRemaining > 0) {
    asio::read(mSocket, responseBuf, asio::transfer_exactly(bytesRemaining), e);

    if (e && e != asio::error::eof) {
      spdlog::error("Read error: {}", e.message());
    }
  }

  assert(responseBuf.size() == contentSize);

  char *data = static_cast<char *>(malloc(contentSize));
  asio::buffer_copy(asio::buffer(data, contentSize), responseBuf.data());

  return {HttpResult::eOk, HttpContentType::eApplicationJson,
          ScopeCreate<Buffer>(data, contentSize)};
};

#endif

}; // namespace cbz::net
