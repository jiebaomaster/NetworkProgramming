/**
 * @file chargen.cc
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2021-12-22
 * 
 * chargen 协议。一直发送数据，不接受数据。
 * 使用 onWriteComplete 事件保证发送数据的速度不快于客户端接受速度
 */
#include <stdio.h>
#include <unistd.h>

#include "../reactor/EventLoop.h"
#include "../reactor/TcpServer.h"

using namespace jmuduo;
using namespace std;

string message;

void onConnection(const TcpConnectionPtr& conn) {
  if (conn->connected()) {
    printf("onConnection(): new connection [%s] from %s\n",
           conn->getName().c_str(), conn->getPeerAddr().toHostPort().c_str());
    conn->send(message);
  } else {
    printf("onConnection(): connection [%s] is down\n",
           conn->getName().c_str());
  }
}

void onWriteComplete(const TcpConnectionPtr& conn) { 
  printf("onWriteComplete(): onWriteComplete [%s]\n",
           conn->getName().c_str());
  conn->send(message);
}

void onMessage(const TcpConnectionPtr& conn, Buffer* buf,
               Timestamp receiveTime) {
  printf("onMessage(): received %zd bytes from connection [%s] at %s\n",
         buf->readableBytes(), conn->getName().c_str(),
         receiveTime.toFormattedString().c_str());
  buf->retrieveAll();
}

int main() {
  printf("main(): pid = %d\n", getpid());

  string line;
  // ascii 码 33-127 之间所有可打印的字符，line=[33-127]
  for (int i = 33; i < 127; ++i) {
    line.push_back(i);
  }
  line += line; // line=[33-127,33-127]
  // message 是 127-33 行数据，每一行为 char(i) 开头的连续 72 个字符
  for (size_t i = 0; i < 127 - 33; ++i) {
    message += line.substr(i, 72) + '\n';
  }

  InetAddress listenAddr(9981);
  EventLoop loop;
  TcpServer server(&loop, listenAddr);
  server.setConnectionCallback(onConnection);
  server.setMessageCallback(onMessage);
  server.setWriteCompleteCallback(onWriteComplete);
  server.start();

  loop.loop();
}