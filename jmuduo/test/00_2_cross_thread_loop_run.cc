#include "../base/thread/Thread.h"
#include "../reactor/EventLoop.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

jmuduo::EventLoop *g_loop;

void threadFunc() {
  // 在子线程运行 loop，应该马上退出
  g_loop->loop();
}

int main() {
  // 在主线程创建loop
  jmuduo::EventLoop loop;
  g_loop = &loop;

  jmuduo::Thread thread(threadFunc);
  thread.start();
  thread.join();
}