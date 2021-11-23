#ifndef _JMUDUO_TIMER_ID_H_
#define _JMUDUO_TIMER_ID_H_

#include "noncopyable.h"

namespace jmuduo {

class Timer;

/**
 * @brief 用户使用设置定时器后，返回 TimerId 类型给用户，用来取消定时器
 * 所有定时器相关类型中，用户只直接使用 TimerId
 */
class TimerId : copyable {
 public:
  explicit TimerId(Timer* t) : value(t) {}

  // 使用默认的 拷贝/赋值/析构

 private:
  Timer* value;
};

}  // namespace jmuduo

#endif