#include "TcpServer.h"

#include "../base/logging/Logging.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "SocketsOps.h"

using namespace jmuduo;

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr)
    : loop_(loop),
      name_(listenAddr.toHostPort()),
      acceptor_(new Acceptor(loop, listenAddr)),
      started_(false),
      nextConnId_(1) {
  acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this,
                                                std::placeholders::_1,
                                                std::placeholders::_2));
}

TcpServer::~TcpServer() {}

void TcpServer::start() {
  if (!started_) {
    started_ = true;
  }

  if (!acceptor_->listenning()) {
    // 通过事件循环保证线程安全
    loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
  }
}

// 此时 sockfd 代表的 tcp 连接已建立
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr) {
  loop_->assertInLoopThread();
  char buf[32];
  snprintf(buf, sizeof buf, "#%d", nextConnId_);
  ++nextConnId_;
  std::string connName = name_ + buf; // 拼接本条 TCP 连接的名称

  LOG_INFO << "TcpServer::newConnection [" << name_ << "] - new connection ["
           << connName << "] from " << peerAddr.toHostPort();

  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // FIXME poll with zero timeout to double confirm the new connection
  // 新建一个 TcpConnection 代表本条连接
  auto conn = std::make_shared<TcpConnection>(loop_, connName, sockfd,
                                              localAddr, peerAddr);
  connections_[connName] = conn; // 记录本条连接
  // 设置用户回调
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  
  conn->connectEstablished();
}