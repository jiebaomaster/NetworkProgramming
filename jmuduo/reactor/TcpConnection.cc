#include "TcpConnection.h"

#include <functional>

#include "../base/logging/Logging.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Socket.h"
#include "SocketsOps.h"

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
  // channel_ 是 TcpConnection 的成员，所以 channel_ 执行 TcpConnection 注册的回调时
  // TcpConnection 必存在，所以不需要 shared_from_this
  // 注册消息回调          
  channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
  channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
  channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
  channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));
}

TcpConnection::~TcpConnection() {
  LOG_DEBUG << "TcpConnection::dtor[" << name_ << "] at " << this
            << " fd = " << channel_->fd();
}

void TcpConnection::connectEstablished() {
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);
  setState(kConnected);
  channel_->enableReading(); // 开始监听消息可读事件
  // 给用户回调传 shared_ptr，确保用户回调期间 TcpConnection 对象存活
  connectionCallback_(shared_from_this()); // 调用建立该连接时的用户回调
}

void TcpConnection::connectDestroyed() {
  loop_->assertInLoopThread();
  assert(state_ == kConnected);
  setState(kDisconnected);
  // connectDestroyed 在某些情况下会不经过 handleClose 而被直接调用
  channel_->disableAll(); // 使信道失能
  connectionCallback_(shared_from_this());
  // 从 poller 中移除本连接使用信道对应的 pollfd
  loop_->removeChannel(channel_.get());
}

void TcpConnection::handleRead(Timestamp receiveTime) {
  int savedErrno = 0;
  // 读取数据到缓冲区中
  ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
  if(n > 0) { // 读取成功，调用可读用户回调
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
  } else if (n == 0) { // 客户端关闭连接，服务端被动关闭连接
    handleClose();
  } else { // 读取错误
    errno = savedErrno;
    LOG_SYSERR << "TcpConnection::handleRead";
    handleError();
  }
}

void TcpConnection::handleWrite() {
  
}

void TcpConnection::handleClose() {
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpConnection::handleClose state = " << state_;
  assert(state_ == kConnected); // 已连接的连接才能被关闭
  channel_->disableAll(); // 使信道失能
  // 从 server 或 client 中删除本连接，TcpServer::removeConnection
  // 必须在最后一行
  closeCallback_(shared_from_this());
}

void TcpConnection::handleError() {
  int err = sockets::getSocketError(socket_->fd());
  LOG_ERROR << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << err << " " << strerror_tl(err);
}