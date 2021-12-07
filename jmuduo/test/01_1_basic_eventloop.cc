#include <sys/timerfd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../reactor/EventLoop.h"
#include "../reactor/Channel.h"

using namespace jmuduo;

EventLoop* g_loop;

void timeout(Timestamp receiveTime) {
  printf("%s Timeout!\n", receiveTime.toFormattedString().c_str());
  g_loop->quit(); // 时间到了就退出事件循环
}


int main() {
  printf("%s started\n", Timestamp::now().toFormattedString().c_str());
  EventLoop loop;
  g_loop = &loop;

  int timefd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  // 在事件循环中新建一个信道监听定时器 fd 的可读事件
  Channel channel(g_loop, timefd);
  channel.setReadCallback(timeout);
  channel.enableReading();

  struct itimerspec howlong;
  bzero(&howlong, sizeof(howlong));
  howlong.it_value.tv_sec = 5; // 等待 5 秒
  ::timerfd_settime(timefd, 0, &howlong, nullptr);

  loop.loop(); // 开始事件循环

  ::close(timefd);

  return 0;
}
