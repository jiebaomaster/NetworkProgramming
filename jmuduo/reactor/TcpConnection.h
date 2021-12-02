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

// 表示“一次 TCP 连接”，供 client 和 server 使用
// TcpConnection 是不可再生的，一旦连接断开，这个对象就没什么用了。
// TcpConnection 没有发起连接的功能，其构造函数的参数是已经建立好连接的 socketfd
// TcpConnection 对象的生命期是模糊的，用户也可以持有，所以全程要用 shared_ptr
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

  // 连接建立完成后被 TcpServer 调用，此时 TcpConnection 的用户回调已设置
  // 应该只被调用一次
  void connectEstablished();

 private:
  enum StateE {
    kConnecting, // 初始值，正在连接
    kConnected, // 连接已建立
  };

  void setState(StateE s) { state_ = s; }
  void handleRead();

  EventLoop* loop_;
  std::string name_; // 连接名称，格式 ip:port#connIndex
  StateE state_; // 该连接的状态
  const std::unique_ptr<Socket> socket_; // TCP socket
  const std::unique_ptr<Channel> channel_; // 监听读写的事件循环信道
  InetAddress localAddr_; // 本地监听地址
  InetAddress peerAddr_; // 远端地址
  ConnectionCallback connectionCallback_; // 建立该连接时的用户回调
  MessageCallback messageCallback_; // 该连接有消息可读时的用户回调
};

}  // namespace jmuduo

#endif