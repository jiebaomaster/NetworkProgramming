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

void TcpConnection::send(const std::string& message) {
  if(state_ == kConnected) {
    if (loop_->isInLoopThread()) { // 在 IO 线程时直接执行，避免函数参数的拷贝
      sendInLoop(message);
    } else { // 在其他线程，转移到IO线程执行
      // P209 P318 跨线程的函数转移调用涉及函数参数的跨线程传递，最简单的方法就是把数据拷贝一份
      loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, message));
    }
  }
}

/**
 * 消息的发送分为两种情况：
 * 1. 如果当前输出缓冲区中没有数据，则可以尝试直接发送，保证性能
 *    - 如果直接发送只发送了部分数据，则将剩余数据放入输出缓冲区
 * 2. 如果当前输出缓冲区中有数据，为了保证数据的顺序性，应该将数据放入输出缓冲区
 * 输出缓冲区中有数据时，开始关注可写事件，并在 handleWrite 中发送输出缓冲区中的数据
 */
void TcpConnection::sendInLoop(const std::string& message) {
  loop_->assertInLoopThread();
  ssize_t nwrote = 0;
  // 如果当前输出缓冲区中没有数据，则可以尝试直接发送
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
    // 没有反复 write 直到返回 EAGAIN，照顾连接公平性，防止有大量数据写出的连接一直占用
    nwrote = ::write(socket_->fd(), message.c_str(), message.size());
    if (nwrote >= 0) {  // 写入成功
      // 数据没有完全写入
      if (static_cast<size_t>(nwrote) < message.size()) {
        LOG_TRACE << "I am going to write more data";
      } else if (writeCompleteCallback_) { // 数据全部写出了，执行回调
        loop_->queueInLoop(
            std::bind(writeCompleteCallback_, shared_from_this()));
      }
    } else {  // 写入失败，没有直接退出，会在下面加入输出缓冲区，再给一次机会
      nwrote = 0;
      if (errno != EWOULDBLOCK) {
        LOG_ERROR << "TcpConnection::sendInLoop";
      }
    }
  }
  assert(nwrote >= 0);
  // 数据没有完全写入 或者 一开始输出缓冲区中就有数据
  if (static_cast<size_t>(nwrote) < message.size()) {
    size_t remaining = message.size() - nwrote;
    size_t oldLen = outputBuffer_.readableBytes();
    if (remaining + oldLen >= highWaterMark_ &&  // 发送缓冲区大小大于高水位
        oldLen < highWaterMark_ &&               // 只在上升沿触发一次
        highWaterMarkCallback_) {
      loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(),
                                   oldLen + remaining));
    }
    // 将数据放入输出缓冲区
    outputBuffer_.append(message.data() + nwrote, message.size() - nwrote);
    if (!channel_->isWriting())  // 开始关注可写事件
      channel_->enableWriting();
  }
}

void TcpConnection::shutdown() {
  // FIXME use compare and swap
  if (state_ == kConnected) {
    // 标记该连接正在关闭，在此状态下应该保证输出缓冲区的数据全部被写出
    setState(kDisconnecting);
    // FIXME shared_from_this()?
    loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
  }
}

void TcpConnection::shutdownInLoop() {
  loop_->assertInLoopThread();
  // 只有当没有数据要写出时，才能关闭写端
  // 这里没能关闭成功的，在 TcpConnection::handleWrite 中进行关闭
  if (!channel_->isWriting()) {
    socket_->shutdownWrite();
  }
}

void TcpConnection::setTcpNoDelay(bool on) {
  socket_->setTcpNoDelay(on);
}

void TcpConnection::connectEstablished() {
  // 应该在 TcpConnection 所处的 ioLoop 中处理新连接的建立
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);
  setState(kConnected);
  channel_->enableReading(); // 开始监听消息可读事件
  // 给用户回调传 shared_ptr，确保用户回调期间 TcpConnection 对象存活
  connectionCallback_(shared_from_this()); // 调用建立该连接时的用户回调
}

void TcpConnection::connectDestroyed() {
  loop_->assertInLoopThread();
  assert(state_ == kConnected || state_ == kDisconnecting);
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
  if (n > 0) {  // 读取成功，调用可读用户回调
    // onMessage 回调中实际上把私有变量 inputBuffer_ 直接暴露给了用户
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
  } else if (n == 0) {  // 客户端关闭连接，服务端被动关闭连接
    handleClose();
  } else {  // 读取错误
    errno = savedErrno;
    LOG_SYSERR << "TcpConnection::handleRead";
    handleError();
  }
}

/**
 * 这里没有反复 write 直到返回 EAGAIN。和 read 时原因差不多。
 * 1. 因为一次 write 没写完，下一次 write 大概率就是 EAGAIN，节省一次系统调用；
 * 2. 照顾连接公平性，防止有大量数据要写出的连接一直占用 IO 线程
 */
void TcpConnection::handleWrite() {
  loop_->assertInLoopThread();
  if (channel_->isWriting()) {
    // 将输出缓冲区中的数据写入 socket
    ssize_t n = ::write(channel_->fd(), outputBuffer_.peek(),
                        outputBuffer_.readableBytes());
    if (n > 0) {  // 发送成功
      outputBuffer_.retrieve(n); // 更新缓冲区可读索引
      if (outputBuffer_.readableBytes() == 0) { // 缓冲区数据全部被写出了
        // 立即不再监听可写事件，防止 busy loop
        channel_->disableWriting();
        // 缓冲区数据全部被写出了，执行回调
        if (writeCompleteCallback_) {
          loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
        }
        // 主动关闭 TCP 连接时因为还有数据要写出而关闭失败的，在这里进行关闭
        if (state_ == kDisconnecting) {
          shutdownInLoop();
        }
      }
    } else {  // 发送失败
      LOG_ERROR << "TcpConnection::handleWrite";
    }
  } else {
    LOG_TRACE << "Connection is down, no more writing";
  }
}

void TcpConnection::handleClose() {
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpConnection::handleClose state = " << state_;
  // 已连接的连接或半关闭的连接才能被关闭
  assert(state_ == kConnected || state_ == kDisconnecting);
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