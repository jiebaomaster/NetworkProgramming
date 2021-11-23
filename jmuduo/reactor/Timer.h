#ifndef _JMUDUO_TIMER_H_
#define _JMUDUO_TIMER_H_

#include "Callbacks.h"
#include "../base/datetime/Timestamp.h"
#include "noncopyable.h"

namespace jmuduo {

/**
 * @brief 表示一个定时器，供定时器事件使用的内部类
 */
class Timer : noncopyable {
 public:
  Timer(const TimerCallback& cb, Timestamp e, double interval)
      : callback_(cb),
        expiration_(e),
        interval_(interval),
        repeat_(interval > 0.0) {}

  // 运行定时器的回调函数
  void run() const { callback_(); }

  Timestamp expiration() const {return expiration_;}
  bool repeat() const {return repeat_;}

  // 重启一个定时器
  void restart(Timestamp now);
 private:
  const TimerCallback callback_;   // 定时器回调函数
  Timestamp expiration_;           // 到期时间
  const double interval_;          // 定时间隔
  const bool repeat_;              // 定时器是否重复
};

}  // namespace jmuduo

#endif
