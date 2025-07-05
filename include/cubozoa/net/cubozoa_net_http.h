#ifndef CBZ_NET_HTTP_H_
#define CBZ_NET_HTTP_H_

#include "cubozoa/memory/cubozoa_memory.h"
#include "cubozoa/net/cubozoa_net.h"

namespace cbz::net {

const std::string HTTP_1_1 = "HTTP/1.1";

enum class HttpMethod {
  eNone,
  eGet,
  ePost,
};

enum class HttpResult {
  eOk = 200,
  eNotFound = 404,

  eHttpCount = 599,
  eInvalidJsonData,
};
enum class HttpContentType {
  eBinary,
  eApplicationJson,
};

CBZ_API class HttpResponse {
public:
  HttpResponse(const HttpResponse &) = delete;
  HttpResponse &operator=(const HttpResponse &) = delete;

  HttpResponse(HttpResponse &&) noexcept = default;
  HttpResponse &operator=(HttpResponse &&) noexcept = default;

  HttpResponse(HttpResult result, std::shared_ptr<Buffer> content);

  const char *readAsCString() const;
  inline HttpResult getResult() const { return mResult; }

private:
  HttpResult mResult;
  Ref<Buffer> mContent;
};

CBZ_API class IHttpClient {
public:
  IHttpClient(const Endpoint &baseAddress);
  virtual ~IHttpClient() = default;

  [[nodiscard]] inline const Endpoint &getBaseAddress() const {
    return mBaseAddress;
  };

  [[nodiscard]] virtual HttpResponse get(const char *path) = 0;

  [[nodiscard]] virtual HttpResponse postJson(const char *path,
                                              const char *jsonString) = 0;

protected:
  // TODO:  MAke this the only virtual. Everything else can just build raw
  // request.
  [[nodiscard]] virtual HttpResponse
  sendRawRequest(const std::string &requestStr) = 0;

private:
  Endpoint mBaseAddress;
};

CBZ_API [[nodiscard]] std::unique_ptr<IHttpClient>
httpClientCreate(const Endpoint &baseAddress);

} // namespace cbz::net

#endif
