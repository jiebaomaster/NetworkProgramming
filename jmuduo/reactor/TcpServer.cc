#include "TcpServer.h"

#include "../base/logging/Logging.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "SocketsOps.h"

#include <assert.h>

using namespace jmuduo;
using std::bind;
using std::placeholders::_1;
using std::placeholders::_2;

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr)
    : loop_(loop),
      name_(listenAddr.toHostPort()),
      acceptor_(new Acceptor(loop, listenAddr)),
      started_(false),
      nextConnId_(1) {
  acceptor_->setNewConnectionCallback(
      bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer() {}

void TcpServer::start() {
  if (!started_) {
    started_ = true;
  }

  if (!acceptor_->listenning()) {
    // 通过事件循环保证线程安全
    loop_->runInLoop(bind(&Acceptor::listen, acceptor_.get()));
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
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(bind(&TcpServer::removeConnection, this, _1));
  conn->connectEstablished();
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnection [" << name_ << "] - connection "
           << conn->getName();
  // 从 map 中删除指定的连接。删除后 conn 的引用计数为 1，如果没有最后一行的 bind，
  // 退出调用该函数的函数时 TcpConnection 会被销毁（conn 是一个引用）
  size_t n = connections_.erase(conn->getName());
  assert(n == 1);
  // 此时仍然在 channel::handleEvent 的执行路径中，为了避免销毁 channel，即避免
  // 销毁 TcpConnection，使用 bind 延长 TcpConnection 的生命周期到下一次事件循
  // 环中执行 connectDestroyed 后，之后 TcpConnection 将自动安全销毁
  loop_->queueInLoop(bind(&TcpConnection::connectDestroyed, conn));
}