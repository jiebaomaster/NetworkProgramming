#include <stdio.h>
#include <unistd.h>

#include "../reactor/Acceptor.h"
#include "../reactor/EventLoop.h"
#include "../reactor/InetAddress.h"
#include "../reactor/SocketsOps.h"

using namespace jmuduo;

void newConnection1(int sockfd, const InetAddress& peerAddr) {
  printf("newConnection(): accepted a new connection from %s\n",
         peerAddr.toHostPort().c_str());
  ::write(sockfd, "I'm xls, how are you?\n", 21);
  sockets::close(sockfd);
}

void newConnection2(int sockfd, const InetAddress& peerAddr) {
  printf("newConnection(): accepted a new connection from %s\n",
         peerAddr.toHostPort().c_str());
  ::write(sockfd, "I'm pps, how are you?\n", 21);
  sockets::close(sockfd);
}

int main() {
  printf("main(): pid=%d\n", getpid());

  EventLoop loop;
  
  // 一个事件循环下有两个 listen socket，他们返回不同的消息

  InetAddress listenAddr1(9981);
  Acceptor accept1(&loop, listenAddr1);
  accept1.setNewConnectionCallback(newConnection1);
  accept1.listen();

  InetAddress listenAddr2(9982);
  Acceptor accept2(&loop, listenAddr2);
  accept2.setNewConnectionCallback(newConnection2);
  accept2.listen();

  loop.loop();
}