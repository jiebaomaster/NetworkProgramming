#include "SocketsOps.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>    // snprintf
#include <strings.h>  // bzero
#include <sys/socket.h>
#include <unistd.h>

#include "../base/logging/Logging.h"

using namespace jmuduo;

namespace {

// 将适用于 ipv4 的地址 sockaddr_in 转换为 socket API 需要的通用地址 sockaddr
const struct sockaddr* sockaddr_cast(const struct sockaddr_in* addr) {
  return reinterpret_cast<const struct sockaddr*>(addr);
}

struct sockaddr* sockaddr_cast(struct sockaddr_in* addr) {
  return reinterpret_cast<struct sockaddr*>(addr);
}

void setNonBlockAndCloseOnExec(int sockfd) {
  // non-block
  int flags = ::fcntl(sockfd, F_GETFL, 0);
  flags |= O_NONBLOCK;
  int ret = ::fcntl(sockfd, F_SETFL, flags);
  if (ret < 0) {
    LOG_SYSFATAL << "setNonBlockAndCloseOnExec";
  }
  // close-on-exec
  flags = ::fcntl(sockfd, F_GETFD, 0);
  flags |= O_CLOEXEC;
  ret = ::fcntl(sockfd, F_SETFD, flags);
  if (ret < 0) {
    LOG_SYSFATAL << "setNonBlockAndCloseOnExec";
  }
}

}  // namespace

int sockets::createNonblockingOrDie() {
  int sockfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  setNonBlockAndCloseOnExec(sockfd);
  
  if (sockfd < 0) {
    LOG_SYSFATAL << "sockets::createNonblockingOrDie";
  }
  return sockfd;
}

void sockets::bindOrDie(int sockfd, const struct sockaddr_in& addr) {
  int ret = ::bind(sockfd, sockaddr_cast(&addr), sizeof addr);
  if (ret < 0) {
    LOG_SYSFATAL << "sockets::bindOrDie";
  }
}

void sockets::listenOrDie(int sockfd) {
  // SOMAXCONN 定义了系统中每一个端口最大的监听队列的长度
  // https://blog.csdn.net/jackyechina/article/details/70992308
  int ret = ::listen(sockfd, SOMAXCONN);
  if (ret < 0) {
    LOG_SYSFATAL << "sockets::listenOrDie";
  }
}

int sockets::accept(int sockfd, struct sockaddr_in* addr) {
  socklen_t addrlen = sizeof *addr;
  int connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
  setNonBlockAndCloseOnExec(connfd);

  if (connfd < 0) {
    int savedErrno = errno;
    LOG_SYSERR << "Socket::accept";
    switch (savedErrno) {
      case EAGAIN:
      case ECONNABORTED:
      case EINTR:
      case EPROTO:  // ???
      case EPERM:
      case EMFILE:  // per-process lmit of open file desctiptor ???
        // expected errors，暂时性的错误，忽略
        errno = savedErrno;
        break;
      case EBADF:
      case EFAULT:
      case EINVAL:
      case ENFILE:
      case ENOBUFS:
      case ENOMEM:
      case ENOTSOCK:
      case EOPNOTSUPP:
        // unexpected errors，致命错误，终止
        LOG_FATAL << "unexpected error of ::accept " << savedErrno;
        break;
      default:
        LOG_FATAL << "unknown error of ::accept " << savedErrno;
        break;
    }
  }
  return connfd;
}

void sockets::close(int sockfd) {
  if (::close(sockfd) < 0) {
    LOG_SYSERR << "sockets::close";
  }
}

void sockets::shutdownWrite(int sockfd) {
  if (::shutdown(sockfd, SHUT_WR) < 0) {
    LOG_SYSERR << "sockets::shutdownWrite";
  }
}

void sockets::toHostPort(char* buf, size_t bufSize,
                         const struct sockaddr_in& addr) {
  char host[INET_ADDRSTRLEN] = "INVALID";
  // 将 网络字节序整数 表示的 ip 地址转换为易读的 点分十进制字符串
  if (::inet_ntop(AF_INET, &addr.sin_addr, host, sizeof host) == nullptr) {
    LOG_SYSERR << "sockets::toHostPort";
  }

  uint16_t port = networkToHost16(addr.sin_port);
  snprintf(buf, bufSize, "%s:%u", host, port);
}

void sockets::fromHostPort(const char* ip, uint16_t port,
                           struct sockaddr_in* addr) {
  addr->sin_family = AF_INET;
  addr->sin_port = hostToNetwork16(port);
  // 将用户传递的 点分十进制字符串 表示的 ip 地址转换为 网络字节序整数
  if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0) {
    LOG_SYSERR << "sockets::fromHostPort";
  }
}

struct sockaddr_in sockets::getLocalAddr(int sockfd) {
  struct sockaddr_in localaddr;
  bzero(&localaddr, sizeof localaddr);
  socklen_t addrlen = sizeof(localaddr);
  if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0) {
    LOG_SYSERR << "sockets::getLocalAddr";
  }
  return localaddr;
}

int sockets::getSocketError(int sockfd) {
  int optval;
  socklen_t optlen = sizeof optval;
  if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
    return errno;
  } else {
    return optval;
  }
}