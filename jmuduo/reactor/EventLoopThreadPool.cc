#include "EventLoopThreadPool.h"
#include "EventLoop.h"
#include "EventLoopThread.h"

using namespace jmuduo;

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop) :
  baseLoop_(baseLoop),
  started_(false),
  numThreads_(0),
  next_(0) {}

EventLoopThreadPool::~EventLoopThreadPool() {
  // loops_ 是相应 IO 线程的栈上对象，不用特意销毁
}

void EventLoopThreadPool::start() {
  assert(!started_);
  baseLoop_->assertInLoopThread();

  started_ = true;
  for (int i = 0; i < numThreads_; i++) {
    // 新建并启动 IO 线程
    threads_.push_back(std::unique_ptr<EventLoopThread>(new EventLoopThread()));
    loops_.push_back(threads_.back()->startLoop());
  }
}

EventLoop* EventLoopThreadPool::getNextLoop() {
  baseLoop_->assertInLoopThread();

  auto loop = baseLoop_; // 默认选择
  if (!loops_.empty()) {
    // round-robin
    loop = loops_[next_++];
    if (static_cast<size_t>(next_) >= loops_.size()) {
      next_ = 0; // 下一轮次
    }
  }

  return loop;
}