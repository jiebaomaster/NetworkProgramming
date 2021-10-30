/**
 * @file client_unblockconnect.c
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2021-10-15
 * 
 * gcc client_unblockconnect.c -o client_ubc && ./client_ubc 127.0.0.1 12345
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
#include <sys/select.h>
#include <unistd.h>
#include <string.h>


#define BUFFER_SIZE 1023

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
 * @brief 超时连接
 * 
 * @param ip 
 * @param port 端口号
 * @param time 超时时间
 * @return int 成功时返回已经处于连接状态的 socket，失败时返回 -1
 */
int unblock_connect(const char *ip, int port, int time) {
  // 创建一个 IPV4 地址
  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;               // protocol ipv4
  inet_pton(AF_INET, ip, &address.sin_addr);  // ip
  address.sin_port = htons(port);             // port

  int sockfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(sockfd >= 0);  

  int fdopt = setnonblocking(sockfd);
  // 尝试连接到远端
  int ret = connect(sockfd, (struct sockaddr *)&address, sizeof(address));
  if(ret == 0) { // 连接成功
    printf("connect with server immediately\n");
    fcntl(sockfd, F_SETFL, fdopt); //
    return sockfd;
  } else if (errno != EINPROGRESS) {
    // 如果连接没有立即建立，那么只有当 EINPROGRESS 时表示连接还在进行，否则出错返回
    printf("unblock connect not support!\n");
    return -1;
  }
  printf("error %d\n", errno);

  /* 1. 连接没有立即建立，调用 select 等待连接建立事件 */
  fd_set writefds;
  struct timeval timeout;
  // 设置要等待 sockfd 的写入事件
  FD_SET(sockfd, &writefds);

  timeout.tv_sec = time;
  timeout.tv_usec = 0;
  // 等待连接建立事件
  ret = select(sockfd + 1, NULL, &writefds, NULL, &timeout);
  // 连接超时或出错，直接返回
  if(ret <= 0) {
    printf("connection time out\n");
    close(sockfd);
    return -1;
  }
  // 发生的事件中没有想要的，返回错误
  if(!FD_ISSET(sockfd, &writefds)) {
    printf("no events on sockfd found\n");
    close(sockfd);
    return -1;
  }

  /* 2. 发生的事件中有 sockfd 的写入事件 */
  int error = 0;
  socklen_t length = sizeof(errno);
  // 调用 getsockopt 获取并清除 sockfd 的错误
  ret = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &errno, &length);
  // 获取失败，直接返回
  if(ret < 0) {
    printf("get socket option failed\n");
    close(sockfd);
    return -1;
  }
  // sockfd 错误码不为 0 表示连接失败
  if(error != 0) {
    printf("connection failed after select with the error: %d\n",error);
    close(sockfd);
    return -1;
  }

  /* 3. 连接成功 */
  printf("connection ready after select with the socket: %d\n", sockfd);
  fcntl(sockfd, F_SETFL, fdopt);
  return sockfd;
}

int main(int argc, char *argv[]) {
  if (argc <= 2) {
    printf("usage: %s ip_address port_number\n", basename(argv[0]));
    return 1;
  }

  const char *ip = argv[1];
  int port = atoi(argv[2]);

  int sockfd = unblock_connect(ip, port, 10);
  if (sockfd < 0) return 1;

  close(sockfd);
  return 0;
}