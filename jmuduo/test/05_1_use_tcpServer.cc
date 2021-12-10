#include <stdio.h>
#include <unistd.h>

#include "../base/datetime/Timestamp.h"
#include "../reactor/Buffer.h"
#include "../reactor/EventLoop.h"
#include "../reactor/TcpServer.h"

using namespace jmuduo;

std::string message1;
std::string message2;

void onConnection(const TcpConnectionPtr& conn) {
  if (conn->connected()) {
    printf("onConnection(): new connection [%s] from %s\n",
           conn->getName().c_str(), conn->getPeerAddr().toHostPort().c_str());
    conn->send(message1);
    conn->send(message2);
    conn->shutdown();
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

int main(int argc, char* argv[]) {
  printf("main(): pid=%d\n", getpid());

  int len1 = 100;
  int len2 = 200;
  
  if (argc > 2) {
    len1 = atoi(argv[1]);
    len2 = atoi(argv[2]);
  }

  message1.resize(len1);
  message2.resize(len2);
  std::fill(message1.begin(), message1.end(), 'A');
  std::fill(message2.begin(), message2.end(), 'B');

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