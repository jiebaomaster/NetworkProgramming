#include "EventLoop.h"
#include "Channel.h"
#include "Poller.h"
#include "TimerQueue.h"
#include "../base/logging/Logging.h"

#include <assert.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <signal.h>

using namespace jmuduo;

// 线程独立变量，保存当前线程下的 事件循环对象指针
__thread EventLoop* t_loopInThisThread = nullptr;
const int kPollTimeMs = 10000; // poll 等待时间

EventLoop* EventLoop::getEventLoopOfCurrentThread() {
  return t_loopInThisThread;
}

/**
 * @brief 使用系统调用新建一个 eventfd
 */
static int createEventfd() {
  int eventfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if(eventfd < 0) {
    LOG_SYSERR << "Failed in eventfd";
    abort();
  }
  return eventfd;
}

class IgnoreSigPipe {
 public:
  IgnoreSigPipe() { ::signal(SIGPIPE, SIG_IGN); }
};
// 实例化了一个全局对象，用来忽略 SIGPIPE 信号，
// 在服务进程繁忙时，没有及时处理对方断开连接的事件，有可能出现在连接断开之后继续发送数据，
// 此时进程会收到 SIGPIPE 信号，其默认行为会使服务进程退出
IgnoreSigPipe initObj;

EventLoop::EventLoop() 
  : looping_(false), 
    quit_(false),
    callingPendingFunctors_(false),
    threadId_(CurrentThread::tid()),
    poller_(new Poller(this)),
    timerQueue_(new TimerQueue(this)),
    wakeupFd_(createEventfd()),
    wakeupChannel_(new Channel(this, wakeupFd_)) {
  LOG_TRACE << "EventLoop created " << this << " in thread " << threadId_;

  if (t_loopInThisThread) { // 一个线程下只能有一个事件循环
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  } else
    t_loopInThisThread = this;


  wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
  // 事件循环运行期间一直监听唤醒信号
  wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
  assert(!looping_);  // 对象销毁时事件循环必须已经停止
  ::close(wakeupFd_);
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
      it->handleEvent(pollReturnTime_);

    doPendingFunctors(); 
  }

  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}

void EventLoop::quit() {
  quit_ = true;
  /**
   * 1. 如果在 IO 线程中执行 quit，当前可能在处理IO事件或者 functor，处理完之后进去
   *    下一次循环前会判断 quit_，自然退出事件循环
   * 2. 如果在别的线程中执行 quit，IO 线程可能正在阻塞等待，为了及时退出需要手动唤醒
   */
  if(!isInLoopThread())
    wakeup();
}

void EventLoop::runInLoop(const Functor& cb) {
  if (isInLoopThread())
    cb();
  else // 加入队列，等待事件循环
    queueInLoop(cb);
}

void EventLoop::queueInLoop(const Functor& cb) {
  {
    MutexLockGuard l(mutex_);
    pendingFunctors_.push_back(cb); // 回调函数加入队列
  }

  /**
   * 每一轮事件循环中事件的执行顺序为，IO 回调 -> functors
   * 1. 如果不在 IO 线程中执行 queueInLoop，为了保证 functor 及时执行，应该唤醒
   * 2. 如果在 IO 线程中，且当前正在执行 functor，因为执行 functor 时可能会继续
   *    注册 functor，为了保证新注册的 functor 及时执行，应该唤醒
   * 3. IO 线程阻塞等待时，不可能发生该函数的调用
   * 4. 换句话说，只有在 IO 线程的 IO 事件回调中才无需唤醒，因为执行完 IO 回调后
   *    自然就会执行 functors，唤醒没有意义
   */
  if(!isInLoopThread() || callingPendingFunctors_)
    wakeup();
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
  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " << CurrentThread::tid();
}

void EventLoop::updateChannel(Channel* channel) {
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel) {
  poller_->removeChannel(channel);
}

void EventLoop::wakeup() {
  uint64_t one = 1;
  // 唤醒阻塞的事件循环只需要往 eventfd 写入，事件循环会监听到 fd 的可读事件
  ssize_t n = ::write(wakeupFd_, &one, sizeof one);
  if(n != sizeof one) {
    LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}

/**
 * 处理事件循环被 eventfd 唤醒，这里只对 eventfd 缓冲区进行了读取，而没有
 * 执行 doPendingFunctors 对唤醒原因 functors 进行真正的处理。实际的 functor
 * 处理放在 EventLoop::loop 中处理 IO 事件之后：
 * 1. 如果 doPendingFunctors 放到 handleRead 中，为了保证及时处理就必须在
 *    queueInLoop 时调用 weakup，这样流程就涉及三个系统调用 write->poll->read，
 *    如果 doPendingFunctors 放到 loop 中，对于“在 IO 线程的 IO 事件处理中注册
 *    functors” 这种情况就可以不经过这三个系统调用，因为执行完 IO 回调后自然就会执
 *    行 functors。结合 queueInLoop 注释第 4 种情况。
 * 2. functors 优先级低于 IO 事件
 */
void EventLoop::handleRead() {
  uint64_t one = 1; // eventfd 的可读缓冲区只有 8B
  // 处理可读事件
  ssize_t n = ::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one) {
    LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
}

void EventLoop::doPendingFunctors() {
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;
  
  /**
   * 没有直接在临界区中依次执行 functors，而是 swap 到局部变量中
   * 1. 减小了临界区长度，functor 的执行时间是不确定的，临界区有可能变得很长
   * 2. 避免了死锁，因为 functor 有可能调用 queueInLoop 而两次锁定 mutex_
   */
  {
    MutexLockGuard l(mutex_);
    functors.swap(pendingFunctors_);
  }

  /**
   * functor 有可能再注册 functor。但这里没有反复执行 doPendingFunctor 直到
   * pendingFunctors_ 为空，因为如果有一个 functor 会在执行完逻辑后将自己再
   * 注册到 functor 中，以求在每一轮事件循环中都执行，这样的 functor 就会造成
   * 死循环执行 doPendingFunctor，导致 IO 线程无法处理 IO 事件。
   * 在这里重新注册 functor 时调用到 queueInLoop 会唤醒事件循环，这样就保证了
   * 及时处理 functor，而且 IO 事件总能被处理。
   */
  for(auto &func : functors)
    func();
  callingPendingFunctors_ = false;
}

