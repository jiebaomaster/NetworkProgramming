#ifndef _JMUDUO_TCP_SERVER_H_
#define _JMUDUO_TCP_SERVER_H_

#include <map>
#include <string>

#include "Callbacks.h"
#include "TcpConnection.h"
#include "noncopyable.h"

namespace jmuduo {

class Acceptor;
class EventLoop;

// 用户直接使用的 TCP 服务端接口，管理 accept 获得的 TcpConnection
// 一个 TCP 服务只有一个 listen socket，但可以有多个 TCP socket
class TcpServer : noncopyable {
 public:
  TcpServer(EventLoop* loop, const InetAddress& listenAddr);
  ~TcpServer();

  // 开始 TCP 服务的监听。线程安全，且多次调用无害
  void start();

  // 设置用户回调，有新的连接建立时
  void setConnectionCallback(const ConnectionCallback& cb) {
    connectionCallback_ = cb;
  }
  // 设置用户回调，有某个连接的消息可读性时
  void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }

 private:
  // 该函数本身不是线程安全的，但是只会事件循环中运行
  // 供 Acceptor 回调，处理新连接的建立
  void newConnection(int sockfd, const InetAddress& peerAddr);

  using ConnectionMap = std::map<std::string, TcpConnectionPtr>;

  EventLoop* loop_;         // acceptor 所在的事件循环
  const std::string name_;  // ip:port
  const std::unique_ptr<Acceptor> acceptor_; // 接收新连接的帮助对象
  ConnectionCallback
      connectionCallback_;  // 供 TcpConnection 在每次有连接建立时调用
  MessageCallback messageCallback_;  // 供 TcpConnection 在每次有消息可读时调用
  bool started_;                     // 服务是否启动
  int nextConnId_;  // 下一个连接 socket 的编号，单调递增
  ConnectionMap connections_;  // 所有 TCP 连接，连接名称 => TcpConnection
};

}  // namespace jmuduo

#endif