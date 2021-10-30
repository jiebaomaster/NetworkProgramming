/**
 * @file server_sharememory.cpp
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2021-10-20
 * 
 * g++ -o server server_sharememory.cpp -lrt -g
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
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

#include "../epollutil.h"
#include "../signalutil.h"

#define USER_LIMIT 5
#define BUFFER_SIZE 1024
#define FD_LIMIT 65535
#define TIMESLOT 5
#define MAX_EVENT_NUMBER 1024

struct client_data {
  sockaddr_in address;  // 客户端socket地址
  int connfd;           // socket
  pid_t pid;            // 处理这个链接的子进程id
  int pipefd[2];        // 和父进程通信的管道
};

// 共享内存的全局唯一标志
static const char *shm_name = "/my_shm";
// 管道 [输出,输入]，使用管道统一事件源
int sig_pipefd[2];
static int epollfd = 0;
static int listenfd;

int shmfd;
// 共享内存的起始地址
char *share_mem = 0;
// 客户连接数组，使用客户连接的编号索引，获取客户连接数据
client_data *users = nullptr;
// 子进程和客户连接的映射关系表，用进程PID索引，获取该进程所处理的客户连接编号
std::unordered_map<int, int> sub_process;
// 当前客户数量
int user_count = 0;
bool stop_child = false;

/**
 * @brief 释放系统资源
 */
void del_resource() {
  close(sig_pipefd[0]);
  close(sig_pipefd[1]);
  close(listenfd);
  close(epollfd);
  shm_unlink(shm_name);
  delete[] users;
}

void child_term_handler(int sig) {
  stop_child = true;
}

/**
 * @brief 子进程运行的代码
 * 
 * @param idx 该子进程处理的客户连接编号
 * @param users 保存所有客户连接数据的数组
 * @param share_mem 共享内存的起始地址
 * @return int 
 */
int run_child(int idx, client_data *users, char *share_mem) {
  epoll_event event[MAX_EVENT_NUMBER];
  // 子进程使用 IO 复用技术来同时监听 客户连接socket 和 与父进程通信的管道socket
  int child_epollfd = epoll_create(5);
  assert(child_epollfd != -1);
  int connfd = users[idx].connfd;
  addfd(child_epollfd, connfd);
  int pipefd = users[idx].pipefd[1];
  addfd(child_epollfd, pipefd);

  int ret;
  // 父进程的信号处理函数不会被继承，需要在子进程中重新设置
  addsig(SIGTERM, child_term_handler, false);

  while (!stop_child) {
    int number = epoll_wait(child_epollfd, event, MAX_EVENT_NUMBER, -1);
    if (number < 0 && number != EINTR) {
      printf("epoll failure\n");
      break;
    }

    for (int i = 0; i < number; i++) {
      int sockfd = event[i].data.fd;
      if (sockfd == connfd && event[i].events & EPOLLIN) {
        // 子进程监听的客户连接 socket 给服务器发来了新数据
        memset(share_mem + BUFFER_SIZE * idx, '\0', BUFFER_SIZE);
        // 读取客户数据到第idx个客户的读缓冲区中
        ret = recv(connfd, share_mem + BUFFER_SIZE * idx, BUFFER_SIZE - 1, 0);
        if (ret < 0) { // 接收失败
          if (errno != EAGAIN) {
            stop_child = true;
          }
        } else if (ret == 0) { // 客户端关闭连接
          stop_child = true;
        } else { // 接收成功，通知父进程编号为idx的客户连接收到了新数据
          send(pipefd, (char *)&idx, sizeof(idx), 0);
        }
      } else if (sockfd == pipefd && event[i].events & EPOLLIN) {
        // 父进程通知本子进程（通过管道）将第client个客户的数据发送到本进程负责的客户端
        int client = 0;
        ret = recv(pipefd, (char *)&client, sizeof(client), 0);
        if (ret < 0) {
          if (errno != EAGAIN) {
            stop_child = true;
          }
        } else if (ret == 0) {
          stop_child = true;
        } else { // 接收成功，转发数据，通过共享内存，避免了数据在多个进程之间拷贝
          send(connfd, share_mem + BUFFER_SIZE * client, BUFFER_SIZE, 0);
        }
      } else {
        printf("something else happened in child process %d\n", idx);
      }
    }
  }

  close(connfd);
  close(pipefd);
  close(child_epollfd);
  return 0;
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
  listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(listenfd >= 0);
  // 绑定地址
  ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
  assert(ret >= 0);
  // 开始监听
  ret = listen(listenfd, 5);
  assert(ret >= 0);

  user_count = 0;
  users = new client_data[USER_LIMIT + 1];

  struct epoll_event events[MAX_EVENT_NUMBER];
  epollfd = epoll_create(5);  // 新建 epoll 内核事件表
  assert(epollfd != -1);
  addfd(epollfd, listenfd);  // 注册事件

  ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
  assert(ret >= 0);
  setnonblocking(sig_pipefd[1]);  // 设置管道的写入端为非阻塞的，加快信号处理
  addfd(epollfd, sig_pipefd[0]);  // 注册输出端的可读事件

  /* 设置一些信号的处理函数 */
  addsig(SIGCHLD);
  addsig(SIGTERM);
  addsig(SIGINT);
  addsig(SIGPIPE, SIG_IGN);

  bool stop_server = false;
  bool terminate = false;

  // 创建共享内存，作为所有用户 socket 连接的读缓存
  shmfd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
  assert(shmfd != -1);
  ret = ftruncate(shmfd, USER_LIMIT * BUFFER_SIZE);
  assert(ret != -1);

  // 将共享内存关联到主进程，共享内存的起始地址为 share_mem
  share_mem = (char*)mmap(NULL, USER_LIMIT * BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
  assert(share_mem != MAP_FAILED);
  close(shmfd);

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
        if (connfd < 0) {
          printf("accept failure, error is %d\n", errno);
          continue;
        }
        // 当前用户已满，拒绝这次连接请求
        if (user_count >= USER_LIMIT) {
          const char *info = "too many users\n";
          printf("%s", info);
          // 返回一个拒绝消息
          send(connfd, info, strlen(info), 0);
          close(connfd);
          continue;
        }
        // 保存第 user_count 个客户连接的相关数据
        users[user_count].address = client_address;
        users[user_count].connfd = connfd;
        // 在主进程和子进程之间建立全双工管道，以传递必要数据
        // 在 fork 之前建立管道，子进程可以继承该管道
        ret = socketpair(PF_UNIX, SOCK_STREAM, 0, users[user_count].pipefd);
        assert(ret != -1);
        pid_t pid = fork();
        if(pid < 0) { // 失败
          close(connfd);
          continue;
        } else if(pid == 0) { // 子进程
          // 子进程会继承父进程打开的文件描述符，关闭其中不必要的
          close(epollfd);
          close(listenfd);
          close(users[user_count].pipefd[0]); // 子进程关闭全双工管道的一端
          close(sig_pipefd[0]);
          close(sig_pipefd[1]);
        
          run_child(user_count, users, share_mem);
          // 解除子进程的共享内存关联
          munmap((void*)share_mem, USER_LIMIT * BUFFER_SIZE);
          // 正常退出，子进程的退出会给父进程发送 SIGCHLD 信号
          exit(0);
        } else { // 父进程
          close(connfd); // 在父进程中关闭当前socket，客户端通信交给子进程处理
          close(users[user_count].pipefd[1]); // 父进程关闭全双工管道的一端
          addfd(epollfd, users[user_count].pipefd[0]); // 监听子进程通过管道与父进程通信
          users[user_count].pid = pid;
          // 记录子进程的用户编号
          sub_process[pid] = user_count;
          user_count++; // 父进程更新当前用户数
          printf("come a new user, now have %d users\n", user_count);
        }
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
              case SIGCHLD:  // 子进程退出，表示有某个客户端关闭了连接
                pid_t pid;
                int stat;
                // 循环处理当前退出的子进程剩余资源
                while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                  int target_uid = sub_process[pid];  // 目标客户id
                  sub_process[pid] = -1;
                  if (target_uid < 0 || target_uid > USER_LIMIT) continue;
                  // 清除目标客户的数据
                  epoll_ctl(epollfd, EPOLL_CTL_DEL, users[target_uid].pipefd[0],
                            0);
                  close(users[target_uid].pipefd[0]);
                  // 拷贝最后一个客户的数据到 target_uid 的位置
                  users[target_uid] = users[--user_count];
                  sub_process[users[target_uid].pid] = target_uid;
                  // 最后一个客户端关闭连接了，如果此时需要退出服务，设置标志
                  if (terminate && target_uid == 0) stop_server = true;
                }
                break;
              case SIGTERM:
              case SIGINT:  // 安全地终止服务器主循环，给所有子进程发送停止信号
                printf("kill all the child now\n");
                if (user_count == 0) {
                  stop_server = true;
                  break;
                }
                // 给所有子进程发送停止信号
                for (int i = 0; i < user_count; ++i)
                  kill(users[i].pid, SIGTERM);
                terminate = true;  // 标志需要退出服务
                break;
              default:
                break;
            }
          }
      } else if (events[i].events & EPOLLIN) {
        // 父进程中除了监听套接字和信号管道套接字，就是子进程通信管道套接字了，处理子进程传送给父进程的数据
        int child = 0;
        // 从子进程通信管道中读取接收到数据的子进程id
        ret = recv(sockfd, (char *)&child, sizeof(child), 0);
        printf("read data from child accross pipe\n");
        if (ret == -1) {
          continue;
        } else if (ret == 0) {
          continue;
        } else {  // 读取成功
          // 使用子进程通信管道通知所有其他子进程，有客户数据需要转发
          for(int i = 0; i < user_count; i++) {
            if (users[i].pipefd[0] != sockfd) {
              printf("send data to child accross pipe\n");
              send(users[i].pipefd[0], (char*)&child, sizeof(child), 0);
            }
          }
        }
      } else {
        printf("something else happened in father process\n");
      }
    }
  }

  del_resource();
  return 0;
}