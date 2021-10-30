#ifndef _SIGNAL_UTIL_H_
#define _SIGNAL_UTIL_H_

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>

// 统一事件源使用的信号管道
extern int sig_pipefd[2];

/**
 * @brief 信号处理函数，转发到管道中成为管道I/O事件
 *
 * @param sig 接收到的信号值
 */
static inline void sig_handler(int sig) {
  // 保留原来的错误信息，在函数最后恢复，以保证函数的可重入性
  int cur_errno = errno;
  char msg = sig;
  // 将信号值写入管道，以通知主循环
  // 信号的总类很少，只需要 1 个字节就可以表示所有信号
  send(sig_pipefd[1], &msg, 1, 0);
  errno = cur_errno;
}

/**
 * @brief 设置信号 sig 的处理函数
 *
 * @param sig
 */
static inline void addsig(int sig, __sighandler_t handler = sig_handler, bool restart = true) {
  struct sigaction sa;
  memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = handler;
  if (restart) sa.sa_flags |= SA_RESTART;  // 重新调用被该信号终止的系统调用
  sigfillset(&sa.sa_mask);
  assert(sigaction(sig, &sa, NULL) != -1);
}

#endif