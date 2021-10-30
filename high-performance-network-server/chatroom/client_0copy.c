/**
 * @file client.c
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2021-10-15
 * 
 * gcc client.c -o client_chatroom && ./client_chatroom 127.0.0.1 12345
 */
#define _GNU_SOURCE 1
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
#include <poll.h>
#include <unistd.h>
#include <string.h>

#define BUFFER_SIZE 64

int main(int argc, char *argv[]) {
  if (argc <= 2) {
    printf("usage: %s ip_address port_number\n", basename(argv[0]));
    return 1;
  }

  const char *ip = argv[1];
  int port = atoi(argv[2]);

  // 创建一个 IPV4 地址
  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;               // protocol ipv4
  inet_pton(AF_INET, ip, &address.sin_addr);  // ip
  address.sin_port = htons(port);             // port

  int sockfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(sockfd >= 0);

  // 尝试连接到远端
  int ret = connect(sockfd, (struct sockaddr *)&address, sizeof(address));
  if (ret < 0) {
    printf("connection failed\n");
    close(sockfd);
    return 1;
  }

  struct pollfd fds[2];
  // 注册标准输入的可读事件
  fds[0].fd = 0;
  fds[0].events = POLLIN;
  fds[0].revents = 0; // 实际发生的事件，由内核设置
  // 注册 sockfd 的可读事件和连接关闭事件
  fds[1].fd = sockfd;
  fds[1].events = POLLIN | POLLRDHUP;
  fds[1].revents = 0;

  char read_buf[BUFFER_SIZE];
  int pipfd[2]; // 管道 [输出,输入]，连接用户输入和 TCPsocket，实现零拷贝
  ret = pipe(pipfd);
  assert(ret != -1);

  while (1) {
    // 监听事件，阻塞直到有注册的事件发生
    ret = poll(fds, 2, -1);
    if (ret < 0) {
      printf("poll failure\n");
      break;
    }

    if (fds[1].revents & POLLRDHUP) { // 服务器主动关闭连接
      printf("server close the connection\n");
      break;
    } else if (fds[1].revents & POLLIN) { // 服务器发来消息
      memset(read_buf, '\0', BUFFER_SIZE);
      recv(fds[1].fd, read_buf, BUFFER_SIZE-1, 0);
      printf("%s\n", read_buf);
    }

    if (fds[0].revents & POLLIN) { // 用户输入，借助管道实现零拷贝
      // 将标准输入重定向到管道输入端
      ret = splice(0, NULL, pipfd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
      // 管道输出重定向到 TCP sockfd
      ret = splice(pipfd[0], NULL, sockfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
    }

  }

  close(sockfd);
  return 0;
}