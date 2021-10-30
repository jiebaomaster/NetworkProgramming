#ifndef _EPOLL_UTIL_H_
#define _EPOLL_UTIL_H_

#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h> 

/**
 * @brief 将文件描述符设置成非阻塞的
 *
 * @param fd
 * @return int
 */
static inline int setnonblocking(int fd) {
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
 */
static inline void addfd(int epollfd, int fd, int flags = 0) {
  struct epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLET;
  if(!flags)
    event.events |= flags;
  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

/**
 * @brief 从 epollfd 指定的 epoll 内核事件表中删除 fd 上所有注册事件
 * 
 * @param epollfd 
 * @param fd 
 */
static inline void removefd(int epollfd, int fd) {
  epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
  close(fd);
}

/**
 * @brief 在 epollfd 指定的 epoll 内核事件表中修改 fd 上的事件
 * 
 * @param epollfd 
 * @param fd 
 * @param ev 时间
 */
static inline void modfd(int epollfd, int fd, int ev) {
  epoll_event event;
  event.data.fd = fd;
  event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
  epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

#endif