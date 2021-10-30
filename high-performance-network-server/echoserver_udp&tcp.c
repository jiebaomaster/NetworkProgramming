/**
 * @file echoserver_udp&tcp.c
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2021-10-16
 * 
 * gcc echoserver_udp&tcp.c -o echo && ./echo 127.0.0.1 12345
 */
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h> 
#include <libgen.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_EVENT_NUMBER 1024
#define TCP_BUFFER_SIZE 512
#define UDP_BUFFER_SIZE 1024

/**
 * @brief 将文件描述符设置成非阻塞的
 *
 * @param fd
 * @return int
 */
int setnonblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}

/**
 * @brief 注册 fd 上的 EPOLLIN 事件到 epollfd 指定的 epoll 内核事件表中
 *
 * @param epollfd 指定 epoll 内核事件表
 * @param fd 注册事件的文件描述符
 */
void addfd(int epollfd, int fd) {
  struct epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLET;
  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

int main(int argc, char *argv[]) {
  if (argc <= 2) {
    printf("usage: %s ip_address, port_number\n", basename(argv[0]));
    return 1;
  }

  const char *ip = argv[1];
  int port = atoi(argv[2]);

  int ret = 0;
  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &address.sin_addr);
  address.sin_port = htons(port);

  /* 创建 TCP 连接 */
  // 新建监听套接字
  int listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(listenfd >= 0);
  // 绑定地址
  ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
  assert(ret >= 0);
  // 开始监听
  ret = listen(listenfd, 5);
  assert(ret >= 0);

  /* 创建 UDP 连接 */
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &address.sin_addr);
  address.sin_port = htons(port);
  int udpfd = socket(PF_INET, SOCK_DGRAM, 0);
  assert(listenfd >= 0);
  // 绑定地址
  ret = bind(udpfd, (struct sockaddr *)&address, sizeof(address));
  assert(ret >= 0);
  // 开始监听
  ret = listen(udpfd, 5);
  assert(ret >= 0);

  /* 在 epoll 中注册 TCP 和 UDP */
  struct epoll_event events[MAX_EVENT_NUMBER];
  int epollfd = epoll_create(5);  // 新建 epoll 内核事件表
  assert(epollfd != -1);
  // 监听socket 不能是 oneshot 的，否则应用程序只能处理一个客户连接
  addfd(epollfd, listenfd);  // 注册事件
  addfd(epollfd, udpfd);

  while (1) {
    // 监听事件的发生
    int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
    if (ret < 0) {
      printf("epoll failture!\n");
      break;
    }

    for (int i = 0; i < ret; i++) {  // 处理每一个事件
      int sockfd = events[i].data.fd;
      if (sockfd == listenfd) {  // TCP 套接字上有新的连接
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);
        // 接受新的连接
        int connfd = accept(listenfd, (struct sockaddr *)&client_address,
                            &client_address_length);
        // 给新的连接套接字注册事件
        addfd(epollfd, connfd);
      } else if(sockfd == udpfd) { // UDP 套接字上有新的消息
        char buf[UDP_BUFFER_SIZE];
        memset(buf, '\0', UDP_BUFFER_SIZE);
        struct sockaddr_in client_address;
        socklen_t client_address_len = sizeof(client_address);
        ret = recvfrom(udpfd, buf, UDP_BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_address, &client_address_len);
        if(ret > 0) // 接收成功，发送回声
          sendto(udpfd, buf, UDP_BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_address, client_address_len);
      } else if (events[i].events & EPOLLIN) {  // TCP 套接字上有新数据
        char buf[TCP_BUFFER_SIZE];
        while (1) {
          memset(buf, '\0', TCP_BUFFER_SIZE);
          // 从套接字中读取 BUFFER_SIZE - 1 个字节的数据
          int ret = recv(sockfd, buf, TCP_BUFFER_SIZE - 1, 0);
          if (ret < 0) {
            /**
             * 对于非阻塞套接字，当满足
             * ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)
             * 时表示数据已经全部读取完毕
             */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              printf("read later\n");
              break;
            }
            close(sockfd);
            break;
          } else if (ret == 0) {  // 通信的客户端已经关闭连接了，服务端也要关闭
            close(sockfd);
          } else { // 读取成功，发送回声
            send(sockfd, buf, ret, 0);
          }
        }
      } else {
        printf("something else happened\n");
      }
    }
  }

  close(listenfd);
  return 0;
}