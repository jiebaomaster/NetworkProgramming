#include "Acceptor.h"

#include "EventLoop.h"
#include "InetAddress.h"
#include "SocketsOps.h"

using namespace jmuduo;

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr)
    : loop_(loop),
      acceptorSocket_(sockets::createNonblockingOrDie()),
      acceptorChannel_(loop, acceptorSocket_.fd()),
      listenning_(false) {
  acceptorSocket_.setReuseAddr(true);
  acceptorSocket_.bindAddress(listenAddr);
  acceptorChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

void Acceptor::listen() {
  loop_->assertInLoopThread();
  listenning_ = true;
  acceptorSocket_.listen();
  // 开始 listen 监听之后才在事件循环中使能信道
  acceptorChannel_.enableReading();
}

void Acceptor::handleRead() {
  loop_->assertInLoopThread();
  InetAddress peerAddr(0);
  // FIXME loop until no more
  int connfd = acceptorSocket_.accept(&peerAddr);  // 接受新的连接
  if (connfd >= 0) {
    if (newConnectionCallback_) {  // 新的连接成功，回调
      // TODO 这里直接把 connfd 传递给 cb，应该先创建一个 Socket
      // 对象，再用移动语意把 Socket 对象 move 给回调函数，确保资源的安全释放
      newConnectionCallback_(connfd, peerAddr);
    } else {
      sockets::close(connfd);
    }
  }
}