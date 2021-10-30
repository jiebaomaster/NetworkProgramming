/**
 * @file webserver.cpp
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2021-10-27
 * 
 * 1. g++ -pthread -o webserver http_conn.cpp webserver.cpp && ./webserver 127.0.0.1 12345
 * 2. sudo mkdir -p /var/www/html && chmod 777 /var/www/html && echo "helloworld!" > /var/www/html/hw.html
 * 3. 使用浏览器访问 http://127.0.0.1:12345/hw.html 可看到 helloworld! 字样
 */
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../epollutil.h"
#include "../../signalutil.h"
#include "../locker.h"
#include "http_conn.h"
#include "threadpool.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

int main(int argc, char *argv[]) {
  if (argc <= 2) {
    printf("usage: %s ip_address, port_number\n", basename(argv[0]));
    return 1;
  }

  const char *ip = argv[1];
  int port = atoi(argv[2]);

  // 忽略 SIGPIPE 信号
  addsig(SIGPIPE, SIG_IGN);

  // 创建线程池
  threadpool<http_conn> *pool = nullptr;
  try {
    pool = new threadpool<http_conn>;
  } catch (...) {
    return 1;
  }

  // 预先为每个可能都客户连接分配一个 http_conn 对象
  http_conn *users = new http_conn[MAX_FD];
  assert(users);
  int user_count = 0;

  // 新建监听套接字
  int listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(listenfd >= 0);
  struct linger tmp = {1, 0};
  // 采用强制关闭模式
  setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

  int ret = 0;
  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &address.sin_addr);
  address.sin_port = htons(port);
  // 绑定地址
  ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
  assert(ret >= 0);
  // 开始监听
  ret = listen(listenfd, 5);
  assert(ret >= 0);

  struct epoll_event events[MAX_EVENT_NUMBER];
  int epollfd = epoll_create(5);  // 新建 epoll 内核事件表
  assert(epollfd != -1);
  // 监听 socket 不能是 oneshot 的，否则应用程序只能处理一个客户连接
  addfd(epollfd, listenfd, EPOLLRDHUP);  // 注册事件
  http_conn::m_epollfd = epollfd;

  while (true) {
    // 监听事件的发生
    int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
    if (ret < 0 && (errno != EAGAIN)) {
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
        if (connfd < 0) {
          printf("accept failed, errno is %d\n", errno);
          continue;
        }
        if (http_conn::m_user_count >= MAX_FD) {  // 检查客户数量
          const char *info = "Internal server busy";
          printf("%s\n", info);
          send(connfd, info, strlen(info), 0);
          close(connfd);
        }
        // 初始化客户连接
        users[connfd].init(connfd, client_address);
      } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        // 客户连接有异常，直接关闭客户连接
        users[sockfd].close_conn();
      } else if (events[i].events & EPOLLIN) { // 连接套接字的 EPOLLIN 事件，表示有客户数据
        // 将 socket 数据读取到缓冲区中，TODO 读取操作是否可以放入线程执行？感觉这样可以提高并发度
        if (users[sockfd].read()) // 如果读取成功，将任务添加到线程池的请求队列
          pool->append(users + sockfd);
        else // 读取失败，关闭客户连接
          users[sockfd].close_conn();
      } else if (events[i].events & EPOLLOUT) { // 如果客户连接 socket 可写
        // 向客户连接 socket 写入 http 请求的 response
        if (!users[sockfd].write()) // 写入失败，关闭客户连接
          users[sockfd].close_conn();
      } else {
        printf("something else happened\n");
      }
    }
  }

  // 释放服务器资源
  close(epollfd);
  close(listenfd);
  delete [] users;
  delete pool;
  return 0;
}