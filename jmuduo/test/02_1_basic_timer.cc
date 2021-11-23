#include "../base/datetime/Timestamp.h"
#include "../reactor/EventLoop.h"
#include "stdio.h"
#include "unistd.h"

using namespace jmuduo;

int cnt = 0;
EventLoop* g_loop;

void printTid() {
  printf("pid = %d, tid = %d\n", getpid(), CurrentThread::tid());
  printf("now %s\n", Timestamp::now().toFormattedString().c_str());
}

void print(const char* msg) {
  printf("msg %s %s\n", Timestamp::now().toFormattedString().c_str(), msg);
}

int main() {
  printTid();
  EventLoop loop;
  g_loop = &loop;
  print("main");
  // 设置定时器
  loop.runAfter(1, [] { print("once1"); });
  loop.runAfter(1.5, [] { print("once1.5"); });
  loop.runAfter(2.5, [] { print("once2.5"); });
  loop.runAfter(3.5, [] { print("once3.5"); });
  loop.runEvery(2, [] { print("every2"); });
  loop.runEvery(3, [] { print("every3"); });

  loop.loop();
  print("main loop exit");
  loop.quit();

  return 0;
}
