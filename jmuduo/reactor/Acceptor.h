#ifndef _JMUDUO_ACCEPTOR_H_
#define _JMUDUO_ACCEPTOR_H_

#include <functional>

#include "Channel.h"
#include "Socket.h"
#include "noncopyable.h"

namespace jmuduo {

class EventLoop;
class InetAddress;

// 内部对象，TcpServer 使用其来接受新的 socket 连接
class Acceptor : noncopyable {
 public:
  // sockfd 新连接套接字，listenAddr 新连接地址
  using NewConnectionCallback =
      std::function<void(int sockfd, const InetAddress& listenAddr)>;

  Acceptor(EventLoop* loop, const InetAddress& listenAddr);

  void setNewConnectionCallback(const NewConnectionCallback& cb) {
    newConnectionCallback_ = cb;
  }

  bool listenning() { return listenning_; }
  // 开始依赖于事件循环的监听
  void listen();

 private:
  // 处理新连接到来事件
  void handleRead();

  EventLoop* loop_; // 连接器所属的事件循环
  Socket acceptorSocket_; // 监听 socket
  Channel acceptorChannel_; // 监听 socket 使用的事件循环信道
  NewConnectionCallback newConnectionCallback_; // 当有新连接到来时的用户回调
  bool listenning_; // 是否正在监听
};

}  // namespace jmuduo

#endif