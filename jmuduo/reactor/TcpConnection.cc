#include "TcpConnection.h"

#include <functional>

#include "../base/logging/Logging.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Socket.h"

#include <unistd.h>
#include <errno.h>

using namespace jmuduo;

TcpConnection::TcpConnection(EventLoop* loop, const std::string& name,
                             int sockfd, const InetAddress& localAddr,
                             const InetAddress& peerAddr)
    : loop_(CHECK_NOTNULL(loop)),
      name_(name),
      state_(kConnecting),
      socket_(new Socket(sockfd)),
      channel_(new Channel(loop, sockfd)),
      localAddr_(localAddr),
      peerAddr_(peerAddr) {
  LOG_DEBUG << "TcpConnection::constructor[" << name_ << "] at" << this
            << " fd=" << sockfd;
  // 注册消息可读回调          
  channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this));
}

TcpConnection::~TcpConnection() {
  LOG_DEBUG << "TcpConnection::dtor[" << name_ << "] at " << this
            << " fd=" << channel_->fd();
}

void TcpConnection::connectEstablished() {
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);
  setState(kConnected);
  channel_->enableReading(); // 开始监听消息可读事件
  // 给用户回调传 shared_ptr，确保用户回调期间 TcpConnection 对象存活
  connectionCallback_(shared_from_this()); // 调用建立该连接时的用户回调
}

void TcpConnection::handleRead() {
  char buf[65536];
  ssize_t n = ::read(socket_->fd(), buf, sizeof buf);
  messageCallback_(shared_from_this(), buf, n); // 调用连接有消息可读时的用户回调
  // FIXME: close connection if n == 0
}