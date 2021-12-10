#include "Socket.h"
#include "InetAddress.h"
#include "SocketsOps.h"
#include "../base/logging/Logging.h"

#include <strings.h> // bzero
#include <netinet/tcp.h>
#include <netinet/in.h>

using namespace jmuduo;

Socket::~Socket() {
  sockets::close(sockfd_);  
}

void Socket::bindAddress(const InetAddress& addr) {
  sockets::bindOrDie(sockfd_, addr.getSockAddrInet());
}

void Socket::listen() {
  sockets::listenOrDie(sockfd_);
}

int Socket::accept(InetAddress *peeraddr) {
  struct sockaddr_in addr;
  bzero(&addr, sizeof addr);
  int connfd = sockets::accept(sockfd_, &addr);
  if(connfd >= 0) {
    peeraddr->setSockAddrInet(addr);
  }

  return connfd;
}

void Socket::shutdownWrite() {
  sockets::shutdownWrite(sockfd_);
}

void Socket::setReuseAddr(bool on) {
  int optval = on ? 1 : 0;
  int ret = ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);
  if(ret < 0) {
    LOG_SYSFATAL << "setsockopt:SO_REUSEADDR";
  }
}