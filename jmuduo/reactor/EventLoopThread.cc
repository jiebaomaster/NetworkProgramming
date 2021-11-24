#include "EventLoopThread.h"

#include <functional>

#include "EventLoop.h"

using namespace jmuduo;

EventLoopThread::EventLoopThread()
    : loop_(nullptr),
      exiting_(false),
      thread_(std::bind(&EventLoopThread::threadFunc, this)),
      mutex_(),
      cond_(mutex_) {}

EventLoopThread::~EventLoopThread() {
  exiting_ = true;
  loop_->quit();
  thread_.join();
}

/**
 * @brief 启动 IO 线程的整个时序是：
 * mian Thread                          IO Thread
 *  |                                    |
 *  |startLoop                           |
 *  |  1) thread_.start();-------------->|threadFunc
 *  |  ----------------cond_.wait()----->|
 *  |                                    |  2) EventLoop loop;
 *  |  <-------------cond_.notify()------|
 *  |  3) return loop_;                  |  loop.loop();
 *  |                                    |  循环处理事件
 */
EventLoop *EventLoopThread::startLoop() {
  thread_.start(); // 开始执行 IO 线程主函数，即 EventLoopThread::threadFunc
  
  {
    MutexLockGuard l(mutex_);
    while (loop_ == nullptr) // 等待 threadFunc 中事件循环对象的创建
      cond_.wait();
  }

  return loop_;
}


void EventLoopThread::threadFunc() {
  // 事件循环对象必须在 IO 线程中创建，因为事件循环对象会记录其所在线程的 tid
  EventLoop loop;

  {
    MutexLockGuard l(mutex_);
    // loop_ 指向一个局部对象，当 threadFunc 退出即事件循环结束后，loop_ 指针将失效
    loop_ = &loop;
    cond_.notify(); // 通知主线程，事件循环对象构造完毕
  }

  loop.loop();
}
