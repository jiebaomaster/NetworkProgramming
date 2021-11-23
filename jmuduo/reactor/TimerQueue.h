#ifndef _JMUDUO_TIMER_QUEUE_H_
#define _JMUDUO_TIMER_QUEUE_H_

#include "noncopyable.h"
#include "Channel.h"
#include "../base/datetime/Timestamp.h"
#include "Callbacks.h"

#include <vector>
#include <set>

namespace jmuduo
{

class EventLoop;
class Timer;
class TimerId;

/**
 * @brief 定时器队列，管理所有的定时器
 * 该定时器队列尽最大努力运行到期的定时器回调，但不保证一定按时，
 * 因为定时器回调的运行依赖事件循环
 */
class TimerQueue : noncopyable {
 public:
  TimerQueue(EventLoop*);
  ~TimerQueue();

  /**
   * @brief 向定时器队列里添加一个定时器
   * 该函数必须是线程安全的，因为其经常在其他线程中调用
   * 
   * @param cb 定时器回调
   * @param t 到期时间
   * @param interval 重复间隔，如果 interval>0.0，则定时器是重复的
   * @return TimerId 返回定时器 id 用来取消定时器
   */
  TimerId addTimer(const TimerCallback& cb, Timestamp t, double interval);

  // TODO 取消定时器
  // void cancel(TimerId timerId);
 private:
  // 定时器队列使用二叉排序树(map/set)，按到期时间从小到大排序
  // 不能直接使用 map<Timestamp, Timer*>，因为可能两个定时器的到期时间相同
  // FIXME 使用 unique_ptr<Timer> 代替 Timer*
  using Entry = std::pair<Timestamp, Timer*>;
  using TimerList = std::set<Entry>;

  // 处理 timerfd 到期事件
  void handleRead();
  // 从定时器队列中取下所有到期的定时器
  std::vector<Entry> getExpired(Timestamp now);
  void reset(const std::vector<Entry>& expired, Timestamp now);

  bool insert(Timer* ptimer);

  EventLoop* loop_; // 该定时器队列所属的事件循环对象
  const int timerfd_; // 定时器队列依赖的文件描述符
  Channel timerfdChannel_; // 定时器队列使用的事件循环信道
  TimerList timers_; // 定时器队列上的所有定时器
};

} // namespace jmuduo


#endif