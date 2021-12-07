#include <stdio.h>
#include <unistd.h>

#include "../base/datetime/Timestamp.h"
#include "../reactor/Buffer.h"
#include "../reactor/EventLoop.h"
#include "../reactor/TcpServer.h"

using namespace jmuduo;

void onConnection(const TcpConnectionPtr& conn) {
  if (conn->connected()) {
    printf("onConnection(): new connection [%s] from %s\n",
           conn->getName().c_str(), conn->getPeerAddr().toHostPort().c_str());
  } else {
    printf("onConnection(): connection [%s] is down\n",
           conn->getName().c_str());
  }
}

void onMessage(const TcpConnectionPtr& conn, Buffer* buf,
               Timestamp receiveTime) {
  printf("onMessage(): received %zd bytes from connection [%s] at %s\n",
         buf->readableBytes(), conn->getName().c_str(),
         receiveTime.toFormattedString().c_str());
  printf("onMessage(): [%s]\n", buf->retrieveAsString().c_str());
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