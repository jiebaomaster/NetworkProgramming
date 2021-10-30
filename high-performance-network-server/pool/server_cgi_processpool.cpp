/**
 * @file server_cgi_processpool.cpp
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2021-10-23
 * 
 * g++ server_cgi_processpool.cpp -o server && ./server 127.0.0.1 12345
 */
#include <stdlib.h>
#include <string.h>

#include "../epollutil.h"
#include "processpool.h"

class cgi_conn {
 public:
  cgi_conn()=default;
  ~cgi_conn(){};

  void init (int epollfd, int sockfd, const sockaddr_in &client_addr) {
    m_epollfd = epollfd;
    m_sockfd = sockfd;
    m_address = client_addr;
    memset(m_buf, '\0', BUFF_SIZE);
    m_read_idx = 0;
  }

  void process() {
    int idx = 0;
    int ret = -1;
    while (true) {
      idx = m_read_idx;
      // 读取客户端发送的数据
      ret = recv(m_sockfd, m_buf + idx, BUFF_SIZE - idx - 1, 0);
      if(ret < 0) { // 读失败
        if(errno != EAGAIN) // 读发生错误，服务端关闭连接
          removefd(m_epollfd, m_sockfd);
        // 暂时没数据可读，退出读循环
        break;
      } else if(ret == 0) { // 客户端关闭连接，服务端也关闭连接
        removefd(m_epollfd, m_sockfd);
      } else {  // 读成功
        m_read_idx += ret;
        printf("user content is: %s\n", m_buf);
        // 如果遇到字符 "\r\n" 则开始处理客户请求
        for (; idx < m_read_idx; idx++) {
          if (idx >= 1 && (m_buf[idx - 1] == '\r' && (m_buf[idx] == '\n'))) break;
        }
        // 如果没有遇到字符 "\r\n"，则需要读取更多客户数据
        if (idx == m_read_idx) continue;
        m_buf[idx - 1] = '\0';

        char *filename = m_buf;
        // 判断用户要执行的 CGI 程序是否存在
        if (access(filename, F_OK) == -1) {
          removefd(m_epollfd, m_sockfd);
          break;
        }
        // 创建子进程来执行 CGI 程序
        ret = fork();
        if (ret == -1) {
          removefd(m_epollfd, m_sockfd);
          break;
        } else if (ret == 0) {  // 子进程
          // 将标准输入重定向到 m_sockfd
          close(STDOUT_FILENO);
          dup(m_sockfd);
          // 执行 CGI 程序
          execl(filename, filename, NULL);
          // 退出子进程
          exit(0);
        } else {  // 父进程
          removefd(m_epollfd, m_sockfd);
          break;
        }
      }
    }
  }
 private:
  // 读缓冲区的大小
  static const int BUFF_SIZE = 1024;
  static int m_epollfd;
  int m_sockfd;
  sockaddr_in m_address;
  // 读缓冲区
  char m_buf[BUFF_SIZE];
  // 标记读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
  int m_read_idx;
};

int cgi_conn::m_epollfd = -1;

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

  processpool<cgi_conn> *pool = processpool<cgi_conn>::create(listenfd);
  if(pool) {
    pool->run();
    delete pool;
  }
  
  // 对象（比如一个文件描述符，或者一段堆内存）由哪个函数创建，就应该由哪个函数销毁
  // mian 函数创建的 socket，在 mian 函数内销毁
  close(listenfd);
  return 0;
}