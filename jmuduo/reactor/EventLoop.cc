#include "EventLoop.h"

#include <assert.h>
#include <poll.h>

#include <iostream>

using namespace jmuduo;

// 线程独立变量，保存当前线程下的 事件循环对象指针
__thread EventLoop* t_loopInThisThread = nullptr;

EventLoop* EventLoop::getEventLoopOfCurrentThread() {
  return t_loopInThisThread;
}

EventLoop::EventLoop() : looping_(false), threadId_(CurrentThread::tid()) {
  std::cout << "EventLoop created " << this << " in thread " << threadId_
            << std::endl;

  if (t_loopInThisThread) { // 一个线程下只能有一个事件循环
    std::cout << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_ << std::endl;
  } else
    t_loopInThisThread = this;
}

EventLoop::~EventLoop() {
  assert(!looping_);  // 对象销毁时事件循环必须已经停止
  t_loopInThisThread = nullptr;
}

void EventLoop::loop() {
  assert(!looping_);
  assertInLoopThread();
  looping_ = true;

  // 什么都不做，等待 5s
  ::poll(NULL, 0, 5 * 1000);

  std::cout << "EventLoop " << this << " stop looping" << std::endl;

  looping_ = false;
}

void EventLoop::abortNotInLoopThread() {
  std::cout << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " << CurrentThread::tid() << std::endl;
}
