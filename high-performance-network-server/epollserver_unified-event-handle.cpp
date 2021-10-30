/**
 * @file server_unified-event-handle.cpp
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2021-10-16
 *
 * @copyright Copyright (c) 2021
 *
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
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "epollutil.h"

#define BUFFER_SIZE 1024
static int pipefd[2];  // 管道 [输出,输入]，使用管道统一事件源

/**
 * @brief 信号处理函数，转发到管道中成为管道I/O事件
 *
 * @param sig 接收到的信号值
 */
void sig_handler(int sig) {
  // 保留原来的错误信息，在函数最后恢复，以保证函数的可重入性
  int cur_errno = errno;
  int msg = sig;
  // 将信号值写入管道，以通知主循环
  // 信号的总类很少，只需要 1 个字节就可以表示所有信号
  send(pipefd[1], (char *)&msg, 1, 0);
  errno = cur_errno;
}

/**
 * @brief 设置信号 sig 的处理函数
 *
 * @param sig
 */
void addsig(int sig) {
  struct sigaction sa;
  memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = sig_handler;
  sa.sa_flags |= SA_RESTART;  // 重新调用被该信号终止的系统调用
  sigfillset(&sa.sa_mask);
  assert(sigaction(sig, &sa, NULL) != -1);
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
  // 新建监听套接字
  int listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(listenfd >= 0);
  // 绑定地址
  ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
  assert(ret >= 0);
  // 开始监听
  ret = listen(listenfd, 5);
  assert(ret >= 0);

  struct epoll_event events[MAX_EVENT_NUMBER];
  int epollfd = epoll_create(5);  // 新建 epoll 内核事件表
  assert(epollfd != -1);
  addfd(epollfd, listenfd);  // 注册事件

  ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
  assert(ret >= 0);
  setnonblocking(pipefd[1]);
  addfd(epollfd, pipefd[0]);  // 注册输出端的可读事件

  /* 设置一些信号的处理函数 */
  addsig(SIGHUP);
  addsig(SIGCHLD);
  addsig(SIGTERM);
  addsig(SIGINT);

  bool stop_server = false;

  while (!stop_server) {
    // 监听事件的发生
    int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
    if (number < 0 && errno != EINTR) {
      printf("epoll failture!\n");
      break;
    }

    char buf[BUFFER_SIZE];
    for (int i = 0; i < number; i++) {  // 处理每一个事件
      int sockfd = events[i].data.fd;
      if (sockfd == listenfd) {  // 监听套接字的 EPOLLIN 事件，表示有新的连接
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);
        // 接受新的连接
        int connfd = accept(listenfd, (struct sockaddr *)&client_address,
                            &client_address_length);
        // 给新的连接套接字注册事件，并指定使用 et 模式
        addfd(epollfd, connfd);
        char *ip = inet_ntoa(client_address.sin_addr);
        printf("get connectiom from %s:%d\n", ip, client_address.sin_port);
      } else if (sockfd == pipefd[0] && events[i].events & EPOLLIN) {
        // 就绪的文件描述符是管道的输出端，则处理信号
        int sig;
        char signals[BUFFER_SIZE];
        int ret = recv(pipefd[0], signals, sizeof(signals), 0);
        if (ret == -1)
          continue;
        else if (ret == 0)
          continue;
        else // 接收成功
          // 每个信号值占 1 字节，逐字节处理信号
          for (int i = 0; i < ret; i++) {
            switch(signals[i]) {
              case SIGCHLD:
              case SIGHUP:
                continue;
              case SIGTERM:
              case SIGINT: // 安全地终止服务器主循环
                stop_server = true;
            }
          }
      } else if (events[i].events & EPOLLIN) {
        // 普通套接字的 EPOLLIN 事件，表示有数据未读出
        // et
        // 模式，套接字有数据未读取事件只会触发一次，需要确保将套接字中所有数据读出
        printf("event trigger once\n");
        while (1) {
          memset(buf, '\0', BUFFER_SIZE);
          // 从套接字中读取 BUFFER_SIZE - 1 个字节的数据
          int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);
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
          } else {  // 读取成功
            printf("get %d bytes of content: %s\n", ret, buf);
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