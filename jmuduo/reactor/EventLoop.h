#ifndef _JMUDUO_EVENTLOOP_H_
#define _JMUDUO_EVENTLOOP_H_

#include <sys/types.h>

#include "../base/noncopyable.h"
#include "../base/thread/Thread.h"

namespace jmuduo
{

/**
 * 事件循环
 * one loop per thread
 * IO 线程将会创建一个事件循环对象
 */
class EventLoop : noncopyable {
 public:
  EventLoop();
  ~EventLoop();

  // 开始事件循环
  void loop();

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
  const pid_t threadId_; // 本事件循环所属的线程 id
};

} // namespace mudu


#endif