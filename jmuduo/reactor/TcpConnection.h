#ifndef _JMUDUO_TCP_CONNCETION_H_
#define _JMUDUO_TCP_CONNCETION_H_

#include <memory>
#include <string>

#include "InetAddress.h"
#include "Callbacks.h"
#include "noncopyable.h"

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
 *            -> loop_->queueInLoop(bind(&TcpConnection::connectDestroyed, conn));
 * 2. 第二轮事件循环，从 Poller.pollfds_ 中删除信道对应的 pollfd
 * TcpConnection::connectDestroyed
 *    -> EventLoop::removeChannel
 *      -> Poller::removeChannel 删除信道对应的 pollfd
 * 3. 连接使用的 socket fd 会在析构函数中自动释放
 * P274 P311 handleEvent 执行时不应该析构其 channel，故在 TcpServer::removeConnection
 * 中使用 bind 延长 TcpConnection 的生命周期到下一次事件循环中执行 connectDestroyed 后，
 * 之后 TcpConnection 将自动安全销毁
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

  // 设置用户回调
  void setConnectionCallback(const ConnectionCallback& cb) {
    connectionCallback_ = cb;
  }
  void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
  /// Internal use only.
  void setCloseCallback(const CloseCallback& cb)
  { closeCallback_ = cb; }

  // 连接建立完成后被 TcpServer 调用，此时 TcpConnection 的用户回调已设置
  // 应该只被调用一次
  void connectEstablished();
  // 本连接已从 TcpServer 后移除后调用，从 poller 中移除本连接使用的信道
  void connectDestroyed();

 private:
  enum StateE {
    kConnecting,    // 初始值，正在连接
    kConnected,     // 连接已建立
    kDisconnected,  // 连接已断开
  };

  void setState(StateE s) { state_ = s; }
  // channel 使用的事件回调
  void handleRead();   // 处理连接可读事件
  void handleWrite();  // 处理连接可写事件
  void handleClose();  // 处理连接断开事件
  void handleError();  // 处理连接错误事件

  EventLoop* loop_;
  std::string name_; // 连接名称，格式 ip:port#connIndex
  StateE state_; // 该连接的状态
  const std::unique_ptr<Socket> socket_; // TCP socket
  const std::unique_ptr<Channel> channel_; // 监听读写的事件循环信道
  InetAddress localAddr_; // 本地监听地址
  InetAddress peerAddr_; // 远端地址
  ConnectionCallback connectionCallback_; // 建立该连接时和关闭该连接时的用户回调
  MessageCallback messageCallback_; // 该连接有消息可读时的用户回调
  CloseCallback closeCallback_; // 该连接被关闭时的回调，内部使用
};

}  // namespace jmuduo

#endif