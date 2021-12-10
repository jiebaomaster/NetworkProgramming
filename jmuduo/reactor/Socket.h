#ifndef _JMUDUO_SOCKET_H_
#define _JMUDUO_SOCKET_H_

#include "noncopyable.h"

namespace jmuduo {

class InetAddress;

/**
 * @brief RAII handle，封装 TCP socket fd 的生命周期
 */
class Socket : noncopyable {
 public:
  // sockfd 不由本对象创建
  explicit Socket(int sockfd) : sockfd_(sockfd) {}
  ~Socket();

  int fd() const { return sockfd_; }

  // 地址 localaddr 已使用时 abort
  void bindAddress(const InetAddress& localaddr);
  // 地址 localaddr 已使用时 abort
  void listen();

  /**
   * @brief 接受一个 TCP 连接
   *
   * @param peeraddr 远端地址
   * @return 成功时返回消息 sockfd（non-blocking and close-on-exec），
   * 并设置 peeraddr；失败时返回 -1
   */
  int accept(InetAddress* peeraddr);

  // 关闭 socket 的写入端
  void shutdownWrite();

  // 设置是否复用本地地址 SO_REUSEADDR
  void setReuseAddr(bool on);

 private:
  const int sockfd_;  // listening socket
};

}  // namespace jmuduo

#endif