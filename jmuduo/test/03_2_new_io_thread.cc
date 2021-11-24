#include "../reactor/EventLoop.h"
#include "../reactor/EventLoopThread.h"
#include <stdio.h>
#include <unistd.h>

using namespace jmuduo;

void runInThread()
{
  printf("runInThread(): pid = %d, tid = %d\n",
         getpid(), jmuduo::CurrentThread::tid());
}

int main() {
  printf("runInThread(): pid = %d, tid = %d\n",
         getpid(), jmuduo::CurrentThread::tid());
  // 在主线程中新建一个 IO 线程       
  EventLoopThread loopThread;
  // 启动 IO 线程，获取 IO 线程使用的事件循环对象
  EventLoop *ploop = loopThread.startLoop();
  // 将主线程的函数转移到 IO 线程中执行
  ploop->runInLoop(runInThread);
  sleep(1);
  // 在主线程中设置 IO 线程上的定时器
  ploop->runAfter(2, runInThread);
  sleep(3);
  // 在主线程中控制 IO 线程中事件循环的退出
  ploop->quit();

  printf("exit main()\n");

  return 0;
}