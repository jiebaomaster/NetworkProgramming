#include <unistd.h>
#include <stdio.h>

#include "../reactor/TcpServer.h"
#include "../reactor/EventLoop.h"

using namespace jmuduo;

void onConnection(const TcpConnectionPtr& conn) {
  if (conn->connected()) {
    printf("onConnection(): new connection [%s] from %s\n",
           conn->getName().c_str(), conn->getPeerAddr().toHostPort().c_str());
  } else {
    printf("onConnection(): connection [%s] is down\n", conn->getName().c_str());
  }
}

void onMessage(const TcpConnectionPtr& conn, const char* data,
               ssize_t len) {
  printf("onMessage(): received %zd bytes from connection [%s]\n", len,
         conn->getName().c_str());
}

int main() {
  printf("main(): pid=%d\n", getpid());

  InetAddress listenAddr(9981);
  EventLoop loop;
  // 使用 TcpServer 创建一个 TCP 服务
  TcpServer server(&loop, listenAddr);
  // 回调普通函数，不需要处理 this，所以不用 bind
  server.setConnectionCallback(onConnection);
  server.setMessageCallback(onMessage);
  server.start();

  loop.loop();
}