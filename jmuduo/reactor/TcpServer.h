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
class EventLoopThreadPool;

/**
 * 用户直接使用的 TCP 服务端接口，管理 accept 获得的 TcpConnection。
 * 一个 TCP 服务只有一个 listen socket，但可以有多个 TCP socket。
 * TcpServer 支持多线程和单线程两种模式：
 * 1. 多线程模式下 loop_ 指向 TcpServer 实例（Acceptor）所在线程，
 *    使用 EventLoopThreadPool 作为 IO 线程池（TcpConnection 所在线程）
 * 2. 单线程模式下 Acceptor 和 TcpConnection 都在 loop_ 线程
 */
class TcpServer : noncopyable {
 public:
  TcpServer(EventLoop* loop, const InetAddress& listenAddr);
  ~TcpServer();

  // 设置线程池中线程数量，表示使用多线程模式
  void setThreadNum(int numThreads);

  // 开始 TCP 服务的监听。线程安全，且多次调用无害
  void start();

  // 设置用户回调，有新的连接建立时
  void setConnectionCallback(const ConnectionCallback& cb) {
    connectionCallback_ = cb;
  }
  // 设置用户回调，有某个连接的消息可读性时
  void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
  // 设置用户回调，每次发送缓冲区被清空时调用
  void setWriteCompleteCallback(const WriteCompleteCallback& cb) {
    writeCompleteCallback_ = cb;
  }

 private:
  // 该函数本身不是线程安全的，但是只会在事件循环中运行
  // 供 Acceptor 回调，处理新连接的建立
  void newConnection(int sockfd, const InetAddress& peerAddr);
  // 线程安全
  // 供 TcpConnection 回调，处理连接的删除
  void removeConnection(const TcpConnectionPtr& conn);
  // 该函数本身不是线程安全的，但是只会在事件循环中运行
  void removeConnectionInLoop(const TcpConnectionPtr& conn);

  using ConnectionMap = std::map<std::string, TcpConnectionPtr>;

  EventLoop* loop_;         // acceptor 所在的事件循环
  const std::string name_;  // ip:port
  const std::unique_ptr<Acceptor> acceptor_; // 接收新连接的帮助对象
  // 多线程服务器使用的 IO 线程池
  const std::unique_ptr<EventLoopThreadPool> threadPool_;
  // 用户回调，供 TcpConnection 在每次有连接建立时调用
  ConnectionCallback connectionCallback_;  
  // 用户回调，供 TcpConnection 在每次有消息可读时调用
  MessageCallback messageCallback_;  
  // 用户回调，供 TcpConnection 在每次发送缓冲区被清空时调用
  WriteCompleteCallback writeCompleteCallback_;

  bool started_;  // 服务是否启动
  int nextConnId_;  // 下一个连接 socket 的编号，单调递增
  ConnectionMap connections_;  // 所有 TCP 连接，连接名称 => TcpConnection
};

}  // namespace jmuduo

#endif