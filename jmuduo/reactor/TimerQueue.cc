#include "TimerQueue.h"

#include <assert.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <stdint.h>

#include <functional>

#include "EventLoop.h"
#include "Timer.h"
#include "TimerId.h"
#include "../base/logging/Logging.h"

namespace jmuduo {
namespace detail {

/* 封装系统调用 timefd 相关系统调用 https://www.cnblogs.com/mickole/p/3261879.html */

/**
 * @brief Create a timefd object
 *
 * @return timefd
 */
int create_timefd() {
  int timefd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timefd < 0) {
    LOG_SYSFATAL << "Faild in timerfd_create";
  }

  return timefd;
}

/**
 * @brief 计算时间 when 与当前时间的时间差
 * 
 * @return struct timespec 
 */
struct timespec howMuchTimeFromNow(Timestamp when) {
  int64_t microseconds =
      when.microSecondsSinceEpoch() - Timestamp::now().microSecondsSinceEpoch();

  if (microseconds < 100) microseconds = 100;

  struct timespec ts;
  ts.tv_sec =
      static_cast<time_t>(microseconds / Timestamp::kMicroSecondsPerSecond);
  // 单位为纳秒，1微妙 = 1000纳秒    
  ts.tv_nsec = static_cast<long>(
      (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
  return ts;
}

/**
 * @brief 当定时器到期时，timerfd 发生可读事件，读取 timerfd
 * 
 * @param timerfd 
 * @param now 
 */
void readTimerfd(int timerfd, Timestamp now) {
  uint64_t howmany;
  // 返回超时次数（从上次调用timerfd_settime()启动开始或上次read成功读取开始）
  ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
  LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at "
            << now.toString();

  if (n != sizeof howmany) {
    LOG_ERROR << "TimerQueue::handleRead() reads" << n << " bytes instead of 8";
  }
}

/**
 * @brief 重新设置 timerfd 指定的定时器
 * 
 * @param timerfd 
 * @param expiration 
 */
void resetTimerfd(int timerfd, Timestamp expiration) {
  struct itimerspec newValue, // 新的定时器到期时间
                    oldValue; // 返回定时器这次设置之前的到期时间
  bzero(&newValue, sizeof(newValue));
  bzero(&oldValue, sizeof(oldValue));
  newValue.it_value = howMuchTimeFromNow(expiration);
  // 以相对当前时间的时间差参数设置定时器
  int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
  if (ret) { // 设置失败
    LOG_SYSERR << "timerfd_settime()";
  }
}

}  // namespace detail

}  // namespace jmuduo

using namespace jmuduo;
using namespace detail;
using std::vector;

TimerQueue::TimerQueue(EventLoop* el)
    : loop_(el),
      timerfd_(create_timefd()),
      timerfdChannel_(el, timerfd_),
      timers_() {
  timerfdChannel_.setReadCallback(std::bind(&TimerQueue::handleRead, this));
  timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue() { 
  ::close(timerfd_);
  // 删除所有定时器
  for(auto &timer : timers_)
    delete timer.second;
}

TimerId TimerQueue::addTimer(const TimerCallback& cb, Timestamp t,
                             double interval) {
  Timer* ptimer = new Timer(cb, t, interval);
  // 在 IO 线程中进行定时器添加
  loop_->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, ptimer));
  return TimerId(ptimer);
}

void TimerQueue::addTimerInLoop(Timer *ptimer) {
  // 由于定时器队列没有用锁保护，所以添加定时器只能在 IO 线程执行
  loop_->assertInLoopThread();
  bool earliestChanged = insert(ptimer);
  if (earliestChanged) // 如果新加入的定时器是最早的，需要重设 timerfd
    resetTimerfd(timerfd_, ptimer->expiration());
}

void TimerQueue::handleRead() {
  loop_->assertInLoopThread();
  Timestamp now(Timestamp::now());
  readTimerfd(timerfd_, now);

  auto expired = getExpired(now); // 获取所有到期的定时器
  for(auto &timer : expired) { // 执行到期定时器的回调函数
    timer.second->run();
  }

  // 重设间隔定时器
  reset(expired, now);
}

vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now) {
  vector<Entry> expired;
  Entry sentry = std::make_pair(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
  // 找到定时器队列中第一个未到期的定时器
  auto it = timers_.lower_bound(sentry);
  // 肯定有一个定时器到期
  assert(it == timers_.end() || now < it->first);
  // 拷贝已到期定时器到 expired
  std::copy(timers_.begin(), it, std::back_inserter(expired));
  // 从定时器队列中删除已到期的定时器
  timers_.erase(timers_.begin(), it);

  return expired;
}

/**
 * @brief 重设间隔定时器，开启新一轮的定时器监听
 */
void TimerQueue::reset(const vector<Entry>& expired, Timestamp now) {
  // 到期定时器中如果有间隔定时器，则重新设置
  for (auto& entry : expired) {
    if (entry.second->repeat()) {
      entry.second->restart(now); // 修改定时器的到期时间
      insert(entry.second); // 重新插入定时器队列
    } else {
      // FIXME 转移到空闲定时器列表，以复用 timer
      delete entry.second;
    }
  }


  if (!timers_.empty()) { // 如果定时器队列不为空
    // 当前定时器队列的第一个定时器
    Timestamp nextExpire = timers_.begin()->second->expiration();

    if (nextExpire.valid()) { // 重新设置 timerfd
      resetTimerfd(timerfd_, nextExpire);
    }
  }
}

/**
 * @brief 将一个定时器添加到定时器队列中
 * 
 * @return 返回本次添加的定时器是否最早到期的
 */
bool TimerQueue::insert(Timer* ptimer) {
  bool earliestChanged = false;
  Timestamp when = ptimer->expiration();
  auto it = timers_.begin();
  // 本次添加的定时器是最早到期的
  if (it == timers_.end() || when < it->first) earliestChanged = true;

  auto res = timers_.insert(std::make_pair(when, ptimer));
  assert(res.second);

  return earliestChanged;
}
