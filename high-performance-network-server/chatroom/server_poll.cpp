/**
 * @file server.cpp
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2021-10-16
 *
 * g++ server.cpp -o server && ./server 127.0.0.1 12345
 */
#define _GNU_SOURCE 1
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


#define USER_LIMIT 5    // 最大用户数量
#define BUFF_SIZE 64    // 读缓冲区大小
#define FD_LIMIT 65535  // 文件描述符数量限制

struct client_data {
  struct sockaddr_in sockaddr;  // 客户端地址
  char *write_buf;              // 待写到客户端的数据地址
  char buf[BUFF_SIZE];          // 从客户端读取的数据
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

  // 以空间换时间，分配 FD_LIMIT 个对象，则每个可能的 socket 都可以获得一个对象
  // 则可以用 socket 的值作为数组下标索引对于的 client_data 对象
  client_data *users = new client_data[FD_LIMIT];
  // 为了提高 poll 的性能，用户数量是被限制的
  pollfd fds[USER_LIMIT + 1];
  int user_counter = 0;  // 当前用户数量
  for (int i = 1; i <= USER_LIMIT; i++) {
    fds[i].fd = -1;
    fds[i].events = 0;
  }
  // 一开始只注册监听 socket 的事件
  fds[0].fd = listenfd;
  fds[0].events = POLLIN;
  fds[0].revents = 0;

  while (1) {
    // 阻塞直到事件发生
    ret = poll(fds, user_counter + 1, -1);
    if (ret < 0) {
      printf("poll failure\n");
      break;
    }

    for (int i = 0; i < user_counter + 1; i++) {  // 遍历处理每一个事件
      if (fds[i].fd == listenfd && fds[i].revents & POLLIN) {  // 有新的连接
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int connfd =
            accept(listenfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (connfd < 0) {
          printf("accept failure, error is %d\n", errno);
          continue;
        }
        // 当前用户已满，拒绝这次连接请求
        if (user_counter >= USER_LIMIT) {
          const char *info = "too many users\n";
          printf("%s", info);
          // 返回一个拒绝消息
          send(connfd, info, strlen(info), 0);
          close(connfd);
          continue;
        }
        user_counter++;
        users[connfd].sockaddr = client_addr;
        setnonblocking(connfd);
        fds[user_counter].fd = connfd;
        fds[user_counter].events = POLLIN | POLLERR | POLLRDHUP;
        fds[user_counter].revents = 0;
        printf("come a new user, now have %d users\n", user_counter);
      } else if (fds[i].revents & POLLERR) {  // 有连接错误
        printf("get an error from %d\n", fds[i].fd);
        char errors[100];
        memset(errors, '\0', 100);
        socklen_t length = sizeof(errors);
        // 清除这个错误
        if (getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &errors, &length) < 0)
          printf("get socket option error\n");
        continue;
      } else if (fds[i].revents & POLLRDHUP) {  // 有客户端关闭连接
        // 服务器也关闭相应的连接，并将总用户数减 1
        // 将 users 和 fds 中最后一个有效的元素复制到当前位置
        // 保证注册 poll 时 fds 中 1...user_counter 总是有效的客户连接
        users[fds[i].fd] = users[fds[user_counter].fd];
        close(fds[i].fd);
        fds[i] = fds[user_counter];
        i--;
        user_counter--;
        printf("a client left\n");
      } else if (fds[i].revents & POLLIN) {  // 有客户端消息
        int connfd = fds[i].fd;
        memset(users[connfd].buf, '\0', BUFF_SIZE);
        // 读取客户端发来的数据
        ret = recv(connfd, users[connfd].buf, BUFF_SIZE - 1, 0);
        if (ret < 0) {
          // 若读操作出错，关闭对应客户端的连接
          if (errno != EAGAIN) {
            close(connfd);
            users[connfd] = users[fds[user_counter].fd];
            fds[i] = fds[user_counter];
            i--;
            user_counter--;
          }
        } else if (ret == 0) {  // 客户端连接关闭
        } else {                // 读取成功
          // 通知其他客户端连接 socket，有数据可写
          // 如此处理消息转发，意味着一次 poll，只能有一个 socket 的输入事件被触发
          for (int j = 1; j <= user_counter; j++) {
            if (fds[j].fd == connfd) continue;
            // 注册 POLLOUT 事件，则下一次 poll 时，在下一个 elseif 中处理输出事件
            fds[j].events |= ~POLLIN;
            fds[j].events |= POLLOUT;
            users[fds[j].fd].write_buf = users[connfd].buf;
          }
        }
      } else if (fds[i].revents & POLLOUT) {
        int connfd = fds[i].fd;
        char *write_buf = users[connfd].write_buf;
        if (!write_buf) continue;
        // 转发客户端的输入
        ret = send(connfd, write_buf, strlen(write_buf), 0);
        users[connfd].write_buf = NULL;
        // 写完数据后重新注册输入事件
        fds[i].events |= ~POLLOUT;
        fds[i].events |= POLLIN;
      }
    }
  }

  delete[] users;
  close(listenfd);
  return 0;
}
