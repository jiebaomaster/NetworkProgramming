#ifndef _JMUDUO_EVENT_LOOP_THREAD_H_
#define _JMUDUO_EVENT_LOOP_THREAD_H_

#include "../base/thread/Condition.h"
#include "../base/thread/Mutex.h"
#include "../base/thread/Thread.h"
#include "noncopyable.h"

namespace jmuduo {

class EventLoop;

/**
 * IO 线程的用户接口，每个 IO 拥有一个事件循环（one loop per thread）
 * 使用方式如下：
 * 1. 在主线程中新建一个 IO 线程       
 *    EventLoopThread loopThread;
 * 2. 在主线程中启动 IO 线程，并获取 IO 线程使用的事件循环对象
 *    EventLoop *ploop = loopThread.startLoop();
 * 3. 可以在主线程中对 IO 线程使用的事件循环对象进行跨线程操作
 *    ploop->runInLoop(runInThread);    执行主线程的函数
 *    ploop->runAfter(2, runInThread);  设置定时器
 *    ploop->quit();                    退出事件循环
 */
class EventLoopThread : noncopyable {
 public:
  EventLoopThread();
  ~EventLoopThread();

  /**
   * @brief 启动 IO 线程
   * 
   * @return 返回 IO 线程使用的事件循环对象，供主线程使用
   */
  EventLoop* startLoop();

 private:
  // IO 线程的主函数
  void threadFunc();

  EventLoop* loop_;
  bool exiting_;
  Thread thread_;
  MutexLock mutex_;
  Condition cond_;
};

}  // namespace jmuduo

#endif