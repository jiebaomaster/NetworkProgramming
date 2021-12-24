#ifndef _JMUDUO_TCP_CONNCETION_H_
#define _JMUDUO_TCP_CONNCETION_H_

#include <memory>
#include <string>

#include "InetAddress.h"
#include "Callbacks.h"
#include "noncopyable.h"
#include "Buffer.h"

namespace jmuduo {

class EventLoop;
class Channel;
class Socket;

/**
 * 表示“一次 TCP 连接”，供 client 和 server 使用
 * TcpConnection 是不可再生的，一旦连接断开，这个对象就没什么用了。
 * TcpConnection 没有发起连接的功能，其构造函数的参数是已经建立好连接的 socketfd
 * TcpConnection 对象的生命期是模糊的，用户也可以持有，所以全程要用 shared_ptr
 * 
 * TcpConnection 对象的销毁，需要十分注意对象的生命周期管理，主要做两件事：
 * 1. 第一轮事件循环，从 TcpServer.connections_ 中删除该连接
 * channel::handleEvent 从其使用的信道监听到可读事件开始
 *   -> readCallback_() 回调连接可读事件处理函数
 *      TcpConnection::handleRead 
 *     -> TcpConnection::handleClose 可读事件是连接关闭，调用关闭事件处理函数
 *       -> closeCallback_(shared_from_this())
 *          TcpServer::removeConnection 从 server 中删除该连接
 *            -> loop->ruTcpServer::removeConnectionInLoop
 *            -> ioLoop->queueInLoop(bind(&TcpConnection::connectDestroyed, conn));
 * 2. 第二轮事件循环，从 Poller.pollfds_ 中删除信道对应的 pollfd
 * TcpConnection::connectDestroyed
 *    -> EventLoop::removeChannel
 *      -> Poller::removeChannel 删除信道对应的 pollfd
 * 3. 连接使用的 socket fd 会在析构函数中自动释放
 * P274 P311 handleEvent 执行时不应该析构其 channel，故在 TcpServer::removeConnection
 * 中使用 bind 延长 TcpConnection 的生命周期到下一次事件循环中执行 connectDestroyed 后，
 * 之后 TcpConnection 将自动安全销毁
 * 
 * P192 TCP 连接的关闭分为两种，主动关闭和被动关闭：
 * 1. 主动关闭。分为两步，先关闭本地“写”端（shutdown，发送 FIN），等对方关闭之后，再被动关闭“读”端
 * 2. 被动关闭。时间循环中检测到对方关闭连接的事件，调用 handleClose 关闭 socket 并销毁相关对象
 * 使用 shutdown 而不是 close，保证了对方发送的正在路上的数据，不会被漏收。也就是，在 TCP 这一层面
 * 解决了“当你打算关闭连接时，如何得知对方是否发了一些数据而你还没收到？”的问题。这个问题也可以在更上层
 * 的协议解决，双方商量好不再互发数据，就可以直接断开连接。这种半关闭方式对对方也有要求，对方在 read 
 * 到 0 字节之后会主动关闭连接，一般网络程序都会这样。但是万一对方故意不关闭连接，就会造成连接一直开放，
 * 消耗系统资源，所以必要时应主动调动 handleClose 来强行关闭连接，则 handleClose 得是 public 的。
 */
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection> {
 public:
  TcpConnection(EventLoop* loop, const std::string& name, int sockfd,
                const InetAddress& localAddr, const InetAddress& peerAddr);
  ~TcpConnection();

  EventLoop* getLoop() const { return loop_; }
  const std::string getName() const { return name_; }
  const InetAddress& getLocalAddr() const { return localAddr_; }
  const InetAddress& getPeerAddr() const { return peerAddr_; }
  bool connected() const { return state_ == kConnected; }

  // void send(const void* message, size_t len);
  // 发送消息，线程安全的，可在别的线程调用
  void send(const std::string& message);
  // 主动断开 TCP 连接，实际为关闭写端口，线程安全的，可在别的线程调用
  void shutdown();
  // 设置禁用 Nagle 算法，避免连续发包出现延迟，适用于低延迟网络服务
  void setTcpNoDelay(bool on);

  // 设置用户回调
  void setConnectionCallback(const ConnectionCallback& cb) {
    connectionCallback_ = cb;
  }
  void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
  void setWriteCompleteCallback(const WriteCompleteCallback& cb) {
    writeCompleteCallback_ = cb;
  }
  void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark) {
    highWaterMarkCallback_ = cb;
    highWaterMark_ = highWaterMark;
  }

  /// Internal use only.
  void setCloseCallback(const CloseCallback& cb)
  { closeCallback_ = cb; }

  /**
   * TcpConnection 是个跨线程对象，其由 TcpServer 创建并索引（connections_），
   * 却运行在不同与 TcpServer（多线程模式）的 IO 线程。但是 muduo 并没有使用锁保护
   * TcpServer 和 TcpConnection，而是保证 TcpConnection 的操作总在 IO 线程运行，
   * TcpServer 的操作总在其所在线程运行，来提供线程安全性
   * 
   * 以下为在连接建立和断开时需要对 TcpConnection 进行的操作，均需保证在 IO 线程执行
   */
  // 连接建立完成后被 TcpServer 调用，此时 TcpConnection 的用户回调已设置
  // 应该只被调用一次
  void connectEstablished();
  // 本连接已从 TcpServer 后移除后调用，从 poller 中移除本连接使用的信道
  void connectDestroyed();

 private:
  enum StateE {
    kConnecting,    // 初始值，正在连接
    kConnected,     // 连接已建立
    kDisconnecting, // 连接正在断开，写端已主动断开
    kDisconnected,  // 连接已断开
  };

  void setState(StateE s) { state_ = s; }
  // channel 使用的事件回调
  void handleRead(Timestamp receiveTime);   // 处理连接可读事件
  void handleWrite();  // 处理连接可写事件
  void handleClose();  // 处理连接断开事件
  void handleError();  // 处理连接错误事件

  void sendInLoop(const std::string& message);
  void shutdownInLoop();

  EventLoop* loop_; // 连接所属的事件循环
  std::string name_; // 连接名称，格式 ip:port#connIndex
  StateE state_; // FIXME: use atomic variable 该连接的状态
  const std::unique_ptr<Socket> socket_; // TCP socket
  const std::unique_ptr<Channel> channel_; // 监听读写的事件循环信道
  InetAddress localAddr_; // 本地监听地址
  InetAddress peerAddr_; // 远端地址
  ConnectionCallback connectionCallback_; // 建立该连接时和关闭该连接时的用户回调
  MessageCallback messageCallback_; // 该连接有消息可读时的用户回调
  WriteCompleteCallback writeCompleteCallback_; // 每次发送缓冲区被清空时回调
  HighWaterMarkCallback
      highWaterMarkCallback_;  // 每次发生缓冲区的长度超过用户指定大小时回调（只在上升沿触发一次）
  CloseCallback closeCallback_; // 该连接被关闭时的回调，内部使用
  size_t highWaterMark_; // 发送缓冲区的高水位标志，超过时调用相应回调，单位为字节
  /**
   * 虽然这两个缓冲区都没有用锁保护，但他们都是线程安全的
   * 1. 对于 input buffer，onMessage() 回调始终发生在该 TcpConnection 所属的 IO
   *    线程，应用程序应该在 onMessage() 完成 input buffer 的操作，并且不要把
   *    input buffer 暴露给其他线程。这样所有对 input buffer 的操作都在同一个 IO 线
   *    程，input buffer 不必是线程安全的
   * 2. 对于 output buffer，应用程序不会直接操作他，而是调用 TcpConnection::send() 
   *    来发送数据，后者是线程安全的
   */
  Buffer inputBuffer_; // 用户读取缓冲区
  Buffer outputBuffer_; // 用户写入缓冲区
};

}  // namespace jmuduo

#endif