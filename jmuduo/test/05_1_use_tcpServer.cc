#include <stdio.h>
#include <unistd.h>

#include "../base/datetime/Timestamp.h"
#include "../reactor/Buffer.h"
#include "../reactor/EventLoop.h"
#include "../reactor/TcpServer.h"

using namespace jmuduo;

std::string message1;
std::string message2;
int sleepSeconds = 0;

void onConnection(const TcpConnectionPtr& conn) {
  if (conn->connected()) {
    printf("onConnection(): tid=%d new connection [%s] from %s\n",
           CurrentThread::tid(), conn->getName().c_str(),
           conn->getPeerAddr().toHostPort().c_str());
    // 建立连接后，睡眠一段时间再发送数据，如果在睡眠时客户端连接断开了，
    // 会在发生数据时收到 SIGPIPE，需要忽略此信号，否则服务器进程会退出
    if (sleepSeconds > 0) {
      ::sleep(sleepSeconds);
    }
    if (!message1.empty())
      conn->send(message1);
    if (!message2.empty())
      conn->send(message2);

    conn->shutdown();
  } else {
    printf("onConnection(): tid=%d connection [%s] is down\n",
           CurrentThread::tid(), conn->getName().c_str());
  }
}

void onMessage(const TcpConnectionPtr& conn, Buffer* buf,
               Timestamp receiveTime) {
  printf("onMessage(): tid=%d received %zd bytes from connection [%s] at %s\n",
         CurrentThread::tid(), buf->readableBytes(), conn->getName().c_str(),
         receiveTime.toFormattedString().c_str());
  printf("onMessage(): [%s]\n", buf->retrieveAsString().c_str());
}

int main(int argc, char* argv[]) {
  printf("main(): pid=%d\n", getpid());

  int len1 = 100;
  int len2 = 200;
  
  if (argc > 2) { // 发送数据长度
    len1 = atoi(argv[1]);
    len2 = atoi(argv[2]);
  }
  
  if(argc > 3) { // 连接建立后睡眠时间
    sleepSeconds = atoi(argv[3]);
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
  if (argc > 4) {
    server.setThreadNum(atoi(argv[4]));
  }
  server.start();

  loop.loop();
}