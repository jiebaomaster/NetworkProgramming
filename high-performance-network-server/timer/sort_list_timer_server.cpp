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

#include "../epollutil.h"
#include "../signalutil.h"
#include "sort_list_timer.h"

#define FD_LIMIT 65535
#define TIMESLOT 5
static int sig_pipefd[2];  // 管道 [输出,输入]，使用管道统一事件源
// 利用升序列表管理所有定时器
static sort_timer_list timer_lst;
static int epollfd = 0;

void timer_handler() {
  // 处理定时任务
  timer_lst.tick();
  // 调用一次 alarm 只会引起一次 SIGALRM 信号，重新定时，以不断触发 SIGALRM 信号
  alarm(TIMESLOT);
}

/**
 * @brief 定时器回调函数，处理非活动 socket
 * 删除非活动 socket 注册的事件，并关闭该 socket
 *
 * @param user_data
 */
void handle_unactive_socket(client_data *user_data) {
  assert(user_data);
  epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
  close(user_data->sockfd);
  printf("close fd %d\n", user_data->sockfd);
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
  epollfd = epoll_create(5);  // 新建 epoll 内核事件表
  assert(epollfd != -1);
  addfd(epollfd, listenfd);  // 注册事件

  ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
  assert(ret >= 0);
  setnonblocking(sig_pipefd[1]);  // 设置管道的写入端为非阻塞的，加快信号处理
  addfd(epollfd, sig_pipefd[0]);  // 注册输出端的可读事件

  /* 设置一些信号的处理函数 */
  addsig(SIGTERM);
  addsig(SIGALRM);  // 定时器信号统一处理

  bool stop_server = false;
  // 以空间换时间，分配 FD_LIMIT 个对象，则每个可能的 socket 都可以获得一个对象
  // 则可以用 socket 的值作为数组下标索引对于的 client_data 对象
  client_data *users = new client_data[FD_LIMIT];
  bool timeout = false;  // 标记是否有定时任务需要处理
  alarm(TIMESLOT);       // 触发定时处理

  while (!stop_server) {
    // 监听事件的发生
    int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
    if (number < 0 && errno != EINTR) {
      printf("epoll failture!\n");
      break;
    }

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

        // 存储新的链接的信息
        users[connfd].sockfd = connfd;
        users[connfd].address = client_address;

        // 创建新的连接的定时器，并加入到全局定时器链表
        util_timer *timer = new util_timer;
        timer->user_data = &users[connfd];
        timer->cb_func = handle_unactive_socket;
        time_t cur = time(NULL);
        // 设置超时时间，一个时钟周期 TIMESLOT 秒，3 个周期内没有活动，该 socket 就会被关闭
        timer->expire = cur + 3 * TIMESLOT;
        users[connfd].timer = timer;
        timer_lst.add_timer(timer);  // 加入到全局定时器链表
      } else if (sockfd == sig_pipefd[0] && events[i].events & EPOLLIN) {
        // 就绪的文件描述符是管道的输出端，则处理信号
        int sig;
        char signals[1024];
        int ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
        if (ret == -1)
          continue;
        else if (ret == 0)
          continue;
        else  // 接收成功
          // 每个信号值占 1 字节，逐字节处理信号
          for (int i = 0; i < ret; i++) {
            switch (signals[i]) {
              case SIGALRM:
                // 这里不立即处理定时任务，只是标记有定时任务要处理。因为定时任务的优先级低于IO事件
                timeout = true;
                break;
              case SIGTERM:  // 安全地终止服务器主循环
                stop_server = true;
            }
          }
      } else if (events[i].events & EPOLLIN) {
        // 普通套接字的 EPOLLIN 事件，表示有数据未读出
        // et
        // 模式，套接字有数据未读取事件只会触发一次，需要确保将套接字中所有数据读出
        printf("event trigger once\n");
        int ret = 0;
        util_timer *timer = users[sockfd].timer;
        while (1) {
          memset(users[sockfd].buf, '\0', BUFF_SIZE);
          // 从套接字中读取 BUFF_SIZE - 1 个字节的数据
          ret = recv(sockfd, users[sockfd].buf, BUFF_SIZE - 1, 0);
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
            /*读取发生错误，关闭连接并移除对应定时器*/
            if (errno != EAGAIN) {
              handle_unactive_socket(&users[sockfd]);
              if (timer)  // 该套接字上有定时器，需要移除
                timer_lst.del_timer(timer);
            }
            close(sockfd);
            break;
          } else if (ret == 0) {  // 通信的客户端已经关闭连接了，服务端也要关闭
            handle_unactive_socket(&users[sockfd]);
            if (timer)  // 该套接字上有定时器，需要移除
              timer_lst.del_timer(timer);
          } else {  // 读取成功
            printf("get %d bytes of content: %s\n", ret, users[sockfd].buf);
          }
        }
        // 如果数据读取成功，则更新保活定时器的超时时间，以延迟该连接被关闭的时间
        if (ret > 0) {
          timer->expire = time(NULL) + 3 * TIMESLOT;
          printf("adjust timer once\n");
          timer_lst.add_timer(timer);
        }
      } else {
        printf("something else happened\n");
      }
    }
    // 处理完所有的IO事件之后才处理定时任务。这也会导致定时任务不能精确地按照预期的时间执行
    if (timeout) {
      timer_handler();
      timeout = false;
    }
  }

  close(listenfd);
  close(sig_pipefd[0]);
  close(sig_pipefd[1]);
  delete[] users;
  return 0;
}