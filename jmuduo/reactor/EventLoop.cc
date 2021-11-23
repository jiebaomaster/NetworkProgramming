#include "EventLoop.h"
#include "Channel.h"
#include "Poller.h"
#include "TimerQueue.h"

#include <assert.h>
#include <iostream>

using namespace jmuduo;

// 线程独立变量，保存当前线程下的 事件循环对象指针
__thread EventLoop* t_loopInThisThread = nullptr;
const int kPollTimeMs = 10000; // poll 等待时间

EventLoop* EventLoop::getEventLoopOfCurrentThread() {
  return t_loopInThisThread;
}

EventLoop::EventLoop() 
  : looping_(false), 
    quit_(false),
    threadId_(CurrentThread::tid()),
    poller_(new Poller(this)),
    timerQueue_(new TimerQueue(this)) {
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
  quit_ = false;

  while (!quit_)  {
    activeChannels_.clear(); // 每一轮事件循环前清空活动信道列表
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_); // 等待事件到来
    for(auto it : activeChannels_) // 遍历处理每一个活动信道的事件
      it->handleEvent();
  }

  std::cout << "EventLoop " << this << " stop looping" << std::endl;

  looping_ = false;
}

void EventLoop::quit() {
  quit_ = true;
  // TODO 退出事件循环的方式为设置标志位，起码要等到本轮事件循环结束才生效，
  // 可以优化为通过某种方式（如 eventfd）及时唤醒 poll
}

TimerId EventLoop::runAt(const Timestamp time, const TimerCallback& cb) {
  return timerQueue_->addTimer(cb, time, 0.0);
}

TimerId EventLoop::runAfter(double delay, const TimerCallback& cb) {
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, cb);
}

TimerId EventLoop::runEvery(double interval, const TimerCallback& cb) {
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(cb, time, interval);
}

void EventLoop::abortNotInLoopThread() {
  std::cout << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " << CurrentThread::tid() << std::endl;
}

void EventLoop::updateChannel(Channel* channel) {
  poller_->updateChannel(channel);
}
