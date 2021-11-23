#ifndef _JMUDUO_EVENTLOOP_H_
#define _JMUDUO_EVENTLOOP_H_

#include <sys/types.h>
#include <memory>
#include <vector>

#include "../base/noncopyable.h"
#include "../base/thread/Thread.h"
#include "Callbacks.h"
#include "TimerId.h"

namespace jmuduo
{

class Channel;
class Poller;
class TimerQueue;

/**
 * 事件循环
 * one loop per thread
 * IO 线程将会创建一个事件循环对象
 */
class EventLoop : noncopyable {
 public:
  EventLoop();
  ~EventLoop();

  // 开始事件循环，只能在事件循环所属线程调用
  void loop();
  // 结束事件循环
  void quit();

  Timestamp pollReturnTime() { return pollReturnTime_; }

  /* 定时器操作接口 */
  /**
   * @brief 在某个时间点 time 运行回调函数 cb
   * 
   * @return TimerId 用于取消定时器
   */
  TimerId runAt(const Timestamp time, const TimerCallback& cb);

  /**
   * @brief 在 delay 秒后运行回调函数 cb
   * 
   * @return TimerId 用于取消定时器
   */
  TimerId runAfter(double delay, const TimerCallback& cb);

  /**
   * @brief 每间隔 interval 秒运行一次回调函数 cb
   * 
   * @return TimerId 用于取消定时器
   */
  TimerId runEvery(double interval, const TimerCallback& cb);
  // TODO 取消定时器
  // void cancel(TimerId TimerId);

  // 更新事件循环中某个信道监听的事件，只能在库内部使用
  void updateChannel(Channel*);
  // TODO 支持删除某个信道
  // void removeChannel(Channel*);

  // 包装线程判断
  void assertInLoopThread() {
    if (!isInLoopThread())
      abortNotInLoopThread();
  }

  /**
   * @brief 判断“调用函数的线程”是否是该“事件循环对象所属的线程”
   * muduo 的接口会明确哪些成员函数是线程安全的，可以跨线程调用；哪些成员函数只能在
   * 某个特定的线程调用（主要是 IO 线程）。为此需要提供一些帮助判断线程的工具函数
   */
  bool isInLoopThread() const {
    return threadId_ == CurrentThread::tid();
  }

  // 返回本线程的事件循环对象
  static EventLoop* getEventLoopOfCurrentThread();

private:
  // hook 异常退出事件循环时调用
  void abortNotInLoopThread();

  bool looping_; /* atomic，当前事件循环是否正在运行 */
  bool quit_;
  const pid_t threadId_; // 本事件循环所属的线程 id
  Timestamp pollReturnTime_; // 本轮事件循环返回的时间
  const std::unique_ptr<Poller> poller_; // 本事件循环依赖的 IO复用 对象
  const std::unique_ptr<TimerQueue> timerQueue_; // 本事件循环使用的 定时器队列
  // 本轮事件循环返回的需要处理的事件，因为该变量只被事件循环所属线程操作，故不需要加锁
  std::vector<Channel*> activeChannels_;
};

} // namespace mudu


#endif