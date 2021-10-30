/**
 * @file epollserver.c
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2021-10-14
 * 
 * gcc epollserver.c -o epollserver && ./epollserver 127.0.0.1 12345
 * telnet 127.0.0.1 12345
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
#define BUFFER_SIZE 10

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
 * @param enable_et 是否对 fd 启用 ET 模式
 */
void addfd(int epollfd, int fd, int enable_et) {
  struct epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN;
  if (enable_et) event.events |= EPOLLET;
  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

/**
 * @brief level trigger 电平触发模式
 *
 * @param events 触发事件列表
 * @param number 事件列表长度
 * @param epollfd 指定 epoll 内核事件表
 * @param listenfd 系统中监听套接字
 */
void lt(struct epoll_event *events, int number, int epollfd, int listenfd) {
  char buf[BUFFER_SIZE];
  for (int i = 0; i < number; i++) {  // 处理每一个事件
    int sockfd = events[i].data.fd;
    if (sockfd == listenfd) {  // 监听套接字的 EPOLLIN 事件，表示有新的连接
      struct sockaddr_in client_address;
      socklen_t client_address_length = sizeof(client_address);
      // 接受新的连接
      int connfd = accept(listenfd, (struct sockaddr *)&client_address,
                          &client_address_length);
      // 给新的连接套接字注册事件，并指定使用 lt 模式
      addfd(epollfd, connfd, 0);
    } else if (events[i].events &
               EPOLLIN) {  // 普通套接字的 EPOLLIN
                           // 事件，表示有数据未读出
      // lt 模式，只要套接字读缓存中还有未读出的数据，就会重复通知此事件
      printf("event trigger once\n");
      memset(buf, '\0', BUFFER_SIZE);
      // 从套接字中读取 BUFFER_SIZE - 1 个字节的数据
      int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);
      if (ret < 0) {
        z
        close(sockfd);
        continue;
      }
      printf("get %d bytes of content: %s\n", ret, buf);
    } else {
      printf("something else happened\n");
    }
  }
}

void et(struct epoll_event *events, int number, int epollfd, int listenfd) {
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
      addfd(epollfd, connfd, 1);
      char *ip = inet_ntoa(client_address.sin_addr);
      printf("get connectiom from %s:%d\n", ip, client_address.sin_port);
    } else if (events[i].events &
               EPOLLIN) {  // 普通套接字的 EPOLLIN
                           // 事件，表示有数据未读出
      // et 模式，套接字有数据未读取事件只会触发一次，需要确保将套接字中所有数据读出
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
          if(errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("read later\n");
            break;
          }
          close(sockfd);
          break;
        } else if(ret == 0) { // 通信的客户端已经关闭连接了，服务端也要关闭
          close(sockfd);
        } else { // 读取成功
          printf("get %d bytes of content: %s\n", ret, buf);
        }
      }
    } else {
      printf("something else happened\n");
    }
  }
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
  addfd(epollfd, listenfd, 1);  // 注册事件

  while (1) {
    // 监听事件的发生
    int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
    if (ret < 0) {
      printf("epoll failture!\n");
      break;
    }
    // 使用特定模式处理
    // lt(events, ret, epollfd, listenfd);
    et(events, ret, epollfd, listenfd);
  }

  close(listenfd);
  return 0;
}