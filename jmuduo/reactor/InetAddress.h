#ifndef _MUDUO_INETADDRESS_H_
#define _MUDUO_INETADDRESS_H_

#include <netinet/in.h>

#include <string>

#include "noncopyable.h"

namespace jmuduo {

// 网络地址类，主要是封装网络地址和本地地址的转换
class InetAddress : copyable {
 public:
  // 构造远端地址 任意IP:port
  explicit InetAddress(uint16_t port);
  // 构造远端地址 ip:port
  InetAddress(const std::string& ip, uint16_t port);

  InetAddress(const struct sockaddr_in& addr) : addr_(addr) {}

  // 以 "ip:port" 的形式返回远端地址（网络字节序）
  std::string toHostPort() const;

  const struct sockaddr_in& getSockAddrInet() const { return addr_; }
  void setSockAddrInet(const struct sockaddr_in& addr) { addr_ = addr; }

 private:
  struct sockaddr_in addr_;
};

}  // namespace jmuduo

#endif