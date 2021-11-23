#ifndef _JMUDUO_CALLBACKS_H_
#define _JMUDUO_CALLBACKS_H_

#include "datetime/Timestamp.h"

#include <functional>

namespace jmuduo
{

/* 所有客户可能会用到的回调函数形式定义 */

// 定时器回调函数
using TimerCallback = std::function<void()>;

} // namespace jmuduo


#endif