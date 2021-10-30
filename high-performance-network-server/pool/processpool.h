#ifndef _PROCESSPOOL_H_
#define _PROCESSPOOL_H_

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../epollutil.h"
#include "../signalutil.h"

// 描述进程信息的类
struct process {
  process() : m_pid(-1) {}
  pid_t m_pid;      // 目标子进程的 PID
  int m_pipefd[2];  // 父进程和子进程通信使用的管道
};

/**
 * @brief 进程池类
 *
 * @tparam T 处理逻辑任务的类
 */
template <typename T>
class processpool {
 private:
  // 将构造函数定义为私有的，因此只能通过后面的 create 静态函数来创建
  // processpool 实例
  processpool(int listenfd, int process_number = 8);

 public:
  // 单例模式，保证程序最多创建一个processpool实例，这是程序正确处理信号的必要条件
  static processpool<T>* create(int listenfd, int process_number = 8) {
    if (!m_instance) m_instance = new processpool<T>(listenfd, process_number);
    return m_instance;
  }
  ~processpool() {
    delete [] m_sub_process;
  };
  /**
   * @brief 启动进程池
   *
   */
  void run();

 private:
  void setup_sig_pipe();
  void run_parent();
  void run_child();

 private:
  //  进程池允许的最大子进程数量
  static const int MAX_PROCESS_NUMBER = 16;
  // 每个子进程最多能处理的客户数量
  static const int USER_PER_PROCESS = 65535;
  // epoll 最多能处理的事件数
  static const int MAX_EVENT_NUMBER = 10000;
  // 进程池中的进程总数
  int m_process_number;
  // 子进程在池中的序列号，从0开始，帮助子进程从 m_sub_process 数组中索引本进程的信息
  // 父进程中 m_idx=-1, 子进程中 m_idx>=0，可以区分子进程和父进程
  int m_idx;
  // 每个进程都有一个 epoll 内核事件表，用 m_epollfd 标识
  int m_epollfd;
  // 监听 socket
  int m_listenfd;
  // 子进程通过m_stop 来决定是否停止运行
  int m_stop;
  // 保存所有子进程的描述信息
  process* m_sub_process;
  // 进程池静态实例
  static processpool<T>* m_instance;
};


template <typename T>
processpool<T>* processpool<T>::m_instance = nullptr;

int sig_pipefd[2];

/**
 * @brief 进程池构造函数
 *
 * @tparam T 处理逻辑任务的类
 * @param listenfd 监听
 * socket，必须在创建进程池之前被创建，否则子进程无法直接使用它，
 *                 因为子进程通过复制父进程的文件描述符获取到 listenfd
 * @param process_number 进程池中进程的数量
 */
template <typename T>
processpool<T>::processpool(int listenfd, int process_number)
    : m_listenfd(listenfd),
      m_process_number(process_number),
      m_idx(-1),
      m_stop(false) {
  assert(process_number > 0 && process_number <= MAX_PROCESS_NUMBER);

  /**
   * 在 fork 之前申请 m_sub_process 的内存空间
   * 对于父进程来说，其中保存了所有子进程的信息
   * 对于子进程来说，配合 m_idx 编号，可以保存本进程的信息
   */
  m_sub_process = new process[process_number];
  assert(m_sub_process);

  // 创建子进程，并建立它们和父进程之间的管道
  for (int i = 0; i < process_number; i++) {
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
    assert(ret == 0);

    m_sub_process[i].m_pid = fork();
    assert(m_sub_process[i].m_pid >= 0);
    if (m_sub_process[i].m_pid == 0) {      // 子进程，停止创建
      close(m_sub_process[i].m_pipefd[0]);  // 关闭不必要的管道端
      m_idx = i;                            // 记录当前子进程的编号
      break;
    } else {                                // 父进程，继续创建子进程
      close(m_sub_process[i].m_pipefd[1]);  // 关闭不必要的管道端
      continue;
    }
  }
}

/**
 * @brief 设置信号处理，统一事件源
 *
 * @tparam T
 */
template <typename T>
void processpool<T>::setup_sig_pipe() {
  // 创建 epoll 内核事件表
  m_epollfd = epoll_create(5);
  assert(m_epollfd != -1);
  // 创建信号管道
  int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
  assert(ret != -1);

  setnonblocking(sig_pipefd[1]);  // 设置管道的写入端为非阻塞的，加快信号处理
  addfd(m_epollfd, sig_pipefd[0]);  // 注册输出端的可读事件

  // 设置信号处理函数
  addsig(SIGCHLD);
  addsig(SIGTERM);
  addsig(SIGINT);
  addsig(SIGPIPE, SIG_IGN);
}

/**
 * @brief 运行进程池
 *        父进程中 m_idx=-1，子进程中 m_idx>=0，根据 m_idx
 * 的值判断运行哪一个函数 父进程与子进程的不同在于监听的事件不同
 * @tparam T
 */
template <typename T>
void processpool<T>::run() {
  if (m_idx != -1) {
    run_child();
    return;
  }
  run_parent();
}

template <typename T>
void processpool<T>::run_child() {
  // 内核事件表和信号处理的初始化
  setup_sig_pipe();

  // 子进程可以通过m_idx找到本进程的信息
  int pipefd = m_sub_process[m_idx].m_pipefd[1];
  // 子进程需要监听管道文件描述符 pipefd，因为父进程通过它来通知子进程 accept
  // 新连接
  addfd(m_epollfd, pipefd);

  epoll_event events[MAX_EVENT_NUMBER];
  // 存储处理逻辑任务类的实例的数组
  // 以实例对应的 socket 做索引，以空间换时间
  T* users = new T[USER_PER_PROCESS];
  assert(users);
  int number = 0;
  int ret = -1;

  while (!m_stop) {
    // 监听事件的发生
    number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
    if (number < 0 && errno != EINTR) {
      printf("epoll failture!\n");
      break;
    }

    for (int i = 0; i < number; i++) {  // 处理每一个事件
      int sockfd = events[i].data.fd;
      if (sockfd == pipefd &&
          events[i].events & EPOLLIN) {  // 父进程发送给子进程的消息
        int client = 0;
        // 从父子进程管道中读取数据
        ret = recv(sockfd, (char*)&client, sizeof(client), 0);
        if (ret < 0 && errno != EAGAIN) {
          continue;
        } else if (ret == 0) {
          continue;
        } else {  // 读取成功，说明有新的连接需要接受
          struct sockaddr_in client_address;
          socklen_t client_address_length = sizeof(client_address);
          // 接受新的连接
          int connfd = accept(m_listenfd, (struct sockaddr*)&client_address,
                              &client_address_length);
          if (connfd < 0) {
            printf("accept failure, error is %d\n", errno);
            continue;
          }
          // 监听新连接的消息
          addfd(m_epollfd, connfd);
          // 模版类 T 必须实现 init 方法，以初始化一个客户连接
          users[connfd].init(m_epollfd, connfd, client_address);
        }
      } else if (sockfd == sig_pipefd[0] && events[i].events & EPOLLIN) {
        // 处理子进程接收到的信号
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
              case SIGCHLD:  // 子进程的子进程退出
                pid_t pid;
                int stat;
                // 循环处理当前退出的孙子进程剩余资源
                // 在 CGI 服务中，孙子进程用来执行 CGI 程序
                while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
                  ;
                break;
              case SIGTERM:
              case SIGINT:  // 安全地终止子进程
                m_stop = true;
                break;
              default:
                break;
            }
          }
      } else if (events[i].events & EPOLLIN) {
        // 如果是其他可读数据，那么必然是用户请求到来，处理客户请求
        // 模版类 T 必须实现 process 方法处理客户请求
        users[sockfd].process();
      } else {
        printf("something else happened in father process\n");
        continue;
      }
    }
  }

  // 释放进程资源
  delete[] users;
  users = nullptr;
  close(pipefd);
  close(m_epollfd);
}

template <typename T>
void processpool<T>::run_parent() {
  // 内核事件表和信号处理的初始化
  setup_sig_pipe();

  // 监听 监听socket 的事件
  addfd(m_epollfd, m_listenfd);

  epoll_event events[MAX_EVENT_NUMBER];
  int choosen_sub_process = 0;  // 在 RoundRobin 算法中记录每次选中的子进程编号
  int new_conn = 1;
  int number = 0;
  int ret = -1;

  while (!m_stop) {
    // 监听事件的发生
    number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
    if (number < 0 && errno != EINTR) {
      printf("epoll failture!\n");
      break;
    }

    for (int i = 0; i < number; i++) {  // 处理每一个事件
      int sockfd = events[i].data.fd;
      if (sockfd ==
          m_listenfd) {  // 监听 socket 上的事件代表有新的连接，通知子进程处理
        // P133图8-11 半同步半异步模式，父进程只管理监听 socket，连接 socket
        // 由子进程管理 如果有新连接到来，就采用 Round Robin 方式将其分配给一个子进程处理
        int idx = choosen_sub_process;
        do {  // 遍历子进程数组，找一个还在运行的子进程
          if (m_sub_process[idx].m_pid != -1) break;
          idx = (idx + 1) % m_process_number;  // idx++
        } while (idx != choosen_sub_process);  // 遍历了一轮还没找到就停止

        if (m_sub_process[idx].m_pid ==
            -1) {  // 所有的子进程都退出了，停止父进程
          m_stop;
          break;
        }

        choosen_sub_process = (idx + 1) % m_process_number;  // 更新
        send(m_sub_process[idx].m_pipefd[1], (char*)&new_conn, sizeof(new_conn),
             0);
        printf("send request to child %d\n", idx);
      } else if (sockfd == sig_pipefd[0] && events[i].events & EPOLLIN) {
        // 处理父进程接收到的信号
        int sig;
        char signals[1024];
        ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
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
                  for (int i = 0; i < m_process_number; i++) {
                    if (m_sub_process[i].m_pid == pid) {
                      printf("child %d join\n", i);
                      // 关闭父子通信管道
                      close(m_sub_process[i].m_pipefd[0]);
                      // 标记该子进程已经退出
                      m_sub_process[i].m_pid = -1;
                      break;
                    }
                  }
                  // 如果所有子进程都退出了，则父进程也要退出
                  m_stop = true;
                  for (int i = 0; i < m_process_number; i++) {
                    if (m_sub_process[i].m_pid != -1) {
                      m_stop = false;
                      break;
                    }
                  }
                }
                break;
              case SIGTERM:
              case SIGINT:  // 安全地终止服务器主循环，给所有子进程发送停止信号
                printf("kill all the child now\n");
                // 给所有还没退出的子进程发送停止信号
                // 也可以通过 父子进程通信管道 通知子进程
                for (int i = 0; i < m_process_number; ++i)
                  if (m_sub_process[i].m_pid != -1)
                    kill(m_sub_process[i].m_pid, SIGTERM);
                break;
              default:
                break;
            }
          }
      } else {
        printf("something else happened in father process\n");
        continue;
      }
    }
  }

  // 释放进程资源
  close(m_epollfd);
}

class handler {
  public:
    virtual void process();
    virtual void init();
};

// 用于处理信号的管道，以实现统一事件源
extern int sig_pipefd[2];

#endif