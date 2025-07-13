#include "cbz_pch.h"

#include "cubozoa/net/cubozoa_net_http.h"

#ifdef WEBGPU_BACKEND_WGPU
#include <asio.hpp>
#include <system_error>
#endif

#include <nlohmann/json.hpp>

static std::shared_ptr<spdlog::logger> sLogger;

namespace cbz::net {

Port::Port(uint16_t port) : mVal(port) {
  std::snprintf(mStringBuffer, sizeof(mStringBuffer), "%u", mVal);
};

const char *Port::c_str() const { return mStringBuffer; }

Address::Address(const char *addrString) {
  unsigned int a, b, c, d;

  if (std::sscanf(addrString, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
    // Invalid, default to 0.0.0.0
    mIpv4.address = {0};
    mIpv4.subnetMask = {0};

    memcpy(mStringBuffer, addrString, strlen(addrString) + 1);
  } else {
    mIpv4.address = (a << 24) | (b << 16) | (c << 8) | d;
    mIpv4.subnetMask = 0xFFFFFF00; // 255.255.255.0 default
    //
    uint32_t ip = mIpv4.address;
    std::snprintf((char *)(mStringBuffer), sizeof(mStringBuffer), "%u.%u.%u.%u",
                  (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF,
                  ip & 0xFF);
  }
}

const char *Address::c_str() const { return mStringBuffer; }

class NetworkingContextNativeClient {
public:
  NetworkingContextNativeClient() : mResolver(mIo), mSocket(mIo) {}
  ~NetworkingContextNativeClient() {}

  Result connect(const std::string &endPoint, const std::string service = "") {
    asio::ip::tcp::resolver::results_type endPoints =
        mResolver.resolve(endPoint, service);

    if (!endPoints.size()) {
      return Result::eNetworkFailure;
    }

    asio::connect(mSocket, endPoints);

    sLogger->info("Connection established {}", endPoint + service);

    while (true) {
      std::array<char, 128> buff;

      asio::error_code error;
      mSocket.read_some(asio::buffer(buff), error);

      if (error) {
        if (error == asio::error::eof) {
          sLogger->info("Connection closed by peer!");
          exit(0);
        }

        sLogger->error("{}", error.message());
        exit(-1);
      }

      sLogger->info("meesage: {}", buff.data());
    }

    return Result::eSuccess;
  }

private:
  asio::io_context mIo;
  asio::ip::tcp::resolver mResolver;
  asio::ip::tcp::socket mSocket;
};

class NetworkingContextServer {
public:
  NetworkingContextServer()
      : mAcceptor(mIo, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 13)),
        mSocket(mIo) {}
  ~NetworkingContextServer() {}

  Result start(Port port) {

    sLogger->info("Server started on port: {}", port.c_str());
    while (true) {
      asio::ip::tcp::socket connectionSocket(mIo);

      mAcceptor.accept(connectionSocket);
      sLogger->trace("{} attemping handshake...",
                     connectionSocket.remote_endpoint().address().to_string());

      std::error_code e;
      asio::write(connectionSocket, asio::buffer(makeDayTimeString()), e);

      if (e) {
        sLogger->info("{}", e.message());
        break;
      }
    }

    return Result::eSuccess;
  }

  std::string makeDayTimeString() const {
    time_t now = std::time(0);
    return std::ctime(&now);
  }

private:
  asio::io_context mIo;
  asio::ip::tcp::acceptor mAcceptor;
  asio::ip::tcp::socket mSocket;
};

Address serverAddress("localhost");
Port port = 13;

Result initClient() {
  sLogger = spdlog::stdout_color_mt("cbzclient");
  sLogger->set_level(spdlog::level::trace);
  sLogger->set_pattern("[%^%l%$][NET] %v");

  return Result::eSuccess;
}

Result initServer() {
  sLogger = spdlog::stdout_color_mt("cbzserver");
  sLogger->set_level(spdlog::level::trace);
  sLogger->set_pattern("[%^%l%$][CBZ|NET] %v");

  NetworkingContextServer server;
  server.start(port);
  return Result::eSuccess;
}

}; // namespace cbz::net
