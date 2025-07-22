#ifndef CBZ_NET_H_
#define CBZ_NET_H_

namespace cbz::net {

struct Port {
public:
  Port(uint16_t port);
  Port() = default;

  const char *c_str() const;

private:
  uint16_t mVal = 0;
  char mStringBuffer[6];
};

struct Ipv4 {
  uint32_t address;    // The full 32-bit IPv4 address
  uint32_t subnetMask; // The full 32-bit subnet mask (e.g. 255.255.255.0)

  uint32_t network() const { return address & subnetMask; }
  uint32_t host() const { return address & ~subnetMask; }
};

class Address {
public:
  Address() = default;
  Address(const char *addrString);

  const char *c_str() const;

private:
  Ipv4 mIpv4;
  char mStringBuffer[255];
};

class Endpoint {
public:
  Address address;
  Port port;
};

[[nodiscard]] Result initClient();

[[nodiscard]] Result initServer();

}; // namespace cbz::net

#endif
