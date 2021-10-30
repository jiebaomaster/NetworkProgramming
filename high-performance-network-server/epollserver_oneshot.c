/**
 * @file epollserver_oneshot.c
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2021-10-14
 * 
 * gcc epollserver_oneshot.c -o oneshot -lpthread && ./oneshot 127.0.0.1 12345
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
#include <pthread.h>

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 10

struct fds {
  int epollfd;
  int sockfd;
};

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
 * @param enable_oneshot 是否对 fd 启用只触发一次事件模式
 */
void addfd(int epollfd, int fd, int enable_oneshot) {
  struct epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLET;
  if (enable_oneshot) event.events |= EPOLLONESHOT;
  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

/**
 * @brief 在 epollfd 指定的 epoll 内核事件表中重置 fd 的注册事件，
 * 以保证 fd 上的 EPOLLIN 事件能再次触发
 * 
 * @param epollfd 指定 epoll 内核事件表
 * @param fd 注册事件的文件描述符
 */
void reset_oneshot(int epollfd, int fd) {
  struct epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
  epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event); // 修改内核事件表
}

/**
 * @brief 工作线程，处理 数据接收和数据处理
 * 
 * @param arg 参数
 * @return void* 
 */
void *worker(void *arg) {
  // 解析参数
  int sockfd = ((struct fds*)arg)->sockfd;
  int epollfd = ((struct fds*)arg)->epollfd;
  printf("start new thread to receive data on fd: %d\n", sockfd);
  
  char buf[BUFFER_SIZE]; // 输入缓冲区
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
        reset_oneshot(epollfd, sockfd); // 数据读取完了要重置
        break;
      }
      break;
    } else if (ret == 0) {  // 通信的客户端已经关闭连接了，服务端也要关闭
      printf("foreiner closed the connection\n");
      break;
    } else {
      printf("get %d bytes of content: %s\n", ret, buf);
      sleep(5); // 休眠 5s，模拟数据处理过程
    }
  }
  printf("end thread receiving data on fd: %d\n", sockfd);
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
  // 监听socket 不能是 oneshot 的，否则应用程序只能处理一个客户连接
  addfd(epollfd, listenfd, 0);  // 注册事件

  while (1) {
    // 监听事件的发生
    int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
    if (ret < 0) {
      printf("epoll failture!\n");
      break;
    }

    for (int i = 0; i < ret; i++) {  // 处理每一个事件
      int sockfd = events[i].data.fd;
      if (sockfd == listenfd) {  // 监听套接字的 EPOLLIN 事件，表示有新的连接
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);
        // 接受新的连接
        int connfd = accept(listenfd, (struct sockaddr *)&client_address,
                            &client_address_length);
        // 给新的连接套接字注册事件，并指定使用 oneshot 模式
        addfd(epollfd, connfd, 0);
      } else if (events[i].events &EPOLLIN) {
        // 普通套接字的 EPOLLIN 事件，表示有数据未读出
        // 启动一个新的工作线程处理 数据接收和数据处理
        pthread_t thread;
        struct fds fds_new_worker = { // 工作线程参数
          .epollfd = epollfd,
          .sockfd = sockfd
        };
        pthread_create(&thread, NULL, worker, &fds_new_worker);
      } else {
        printf("something else happened\n");
      }
    }
  }

  close(listenfd);
  return 0;
}