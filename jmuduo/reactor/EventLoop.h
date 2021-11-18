#ifndef _JMUDUO_EVENTLOOP_H_
#define _JMUDUO_EVENTLOOP_H_

#include <sys/types.h>
#include <memory>
#include <vector>

#include "../base/noncopyable.h"
#include "../base/thread/Thread.h"

namespace jmuduo
{

class Channel;
class Poller;

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

  void updateChannel(Channel*);

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
  const std::unique_ptr<Poller> poller_; // 本事件循环依赖的 IO复用 对象
  // 本轮事件循环返回的需要处理的事件，因为该变量只被事件循环所属线程操作，故不需要加锁
  std::vector<Channel*> activeChannels_;
};

} // namespace mudu


#endif