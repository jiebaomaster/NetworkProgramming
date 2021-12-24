#include "TcpServer.h"

#include "../base/logging/Logging.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "SocketsOps.h"
#include "EventLoopThreadPool.h"

#include <assert.h>

using namespace jmuduo;
using std::bind;
using std::placeholders::_1;
using std::placeholders::_2;

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr)
    : loop_(loop),
      name_(listenAddr.toHostPort()),
      acceptor_(new Acceptor(loop, listenAddr)),
      threadPool_(new EventLoopThreadPool(loop)),
      started_(false),
      nextConnId_(1) {
  acceptor_->setNewConnectionCallback(
      bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer() {}

void TcpServer::setThreadNum(int numThreads) {
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}

void TcpServer::start() {
  if (!started_) {
    started_ = true;
    // 建立线程池
    threadPool_->start();
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
  // 从线程池中获取一个事件循环
  auto ioLoop = threadPool_->getNextLoop();
  // 新建一个 TcpConnection 代表本条连接，该连接属于 ioLoop
  auto conn = std::make_shared<TcpConnection>(ioLoop, connName, sockfd,
                                              localAddr, peerAddr);
  connections_[connName] = conn; // 记录本条连接
  // 设置新连接的用户回调
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(bind(&TcpServer::removeConnection, this, _1));
  // 新的连接属于 ioLoop，应该在 ioLoop 中处理对连接的操作
  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
  // TcpConnection 会在自己的 ioLoop 线程调用 removeConnection，所以需要把他移动到
  // TcpServer 的 loop_ 线程（因为 TcpServer 是无锁的）
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn) {
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_ << "] - connection "
           << conn->getName();
  // 从 map 中删除指定的连接。删除后 conn 的引用计数为 1，如果没有最后一行的 bind，
  // 退出调用该函数的函数时 TcpConnection 会被销毁（conn 是一个引用）
  size_t n = connections_.erase(conn->getName());
  assert(n == 1);
  auto ioLoop = conn->getLoop();
  // 此时仍然在 channel::handleEvent 的执行路径中，为了避免销毁 channel，即避免
  // 销毁 TcpConnection，使用 bind 延长 TcpConnection 的生命周期到下一次事件循
  // 环中执行 connectDestroyed 后，之后 TcpConnection 将自动安全销毁。
  // 连接属于 ioLoop，应该在 ioLoop 中处理对连接的操作，
  // 故要将 connectDestroyed 移动到 TcpConnection 的 ioLoop 线程执行。
  ioLoop->queueInLoop(bind(&TcpConnection::connectDestroyed, conn));
}