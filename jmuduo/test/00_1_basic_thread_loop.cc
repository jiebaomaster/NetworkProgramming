/**
 * @file 00_basic_thread&loop.cpp
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2021-11-16
 * 
 * g++ -phtread 
 */
#include "../base/thread/Thread.h"
#include "../reactor/EventLoop.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

void threadFunc() {
  printf("threadFunc(): pid = %d, tid = %d\n",
        getpid(), jmuduo::CurrentThread::tid());

  jmuduo::EventLoop loop;
  loop.loop(); // 运行子线程的 loop
}

int main() {
  printf("main(): pid = %d, tid = %d\n",
        getpid(), jmuduo::CurrentThread::tid());

  jmuduo::EventLoop loop;

  jmuduo::Thread thread(threadFunc);
  thread.start();

  loop.loop(); // 运行主线程的 loop
  pthread_exit(NULL);

  return 0;
}