#ifndef _JMUDUO_EVENT_LOOP_THREAD_POOL_
#define _JMUDUO_EVENT_LOOP_THREAD_POOL_

#include <vector>
#include <memory>

#include "noncopyable.h"


namespace jmuduo {
  
class EventLoop;
class EventLoopThread;

/**
 * IO 线程池。默认线程数量为 0，需要手动设置线程数量
 */
class EventLoopThreadPool : noncopyable {
 public:
  EventLoopThreadPool(EventLoop* baseLoop);
  ~EventLoopThreadPool();
  // 设置线程池中线程的数量
  void setThreadNum(int threadNum) { numThreads_ = threadNum; }
  // 线程池开始运行，建立其中所有的 IO 线程
  void start();
  // 从线程池选取下一个事件循环对象，目前采用最简单的 round-robin 算法选取
  EventLoop* getNextLoop();

 private:
  EventLoop* baseLoop_; // IO 线程池由某个 TcpServer 所有，指向 TcpServer 所在的事件循环
  bool started_; // 线程池是否已启动
  int numThreads_; // 线程池中线程的数量
  int next_; // 下一个待选中的循环索引
  // 本线程池中的所有 IO 线程对象
  std::vector<std::unique_ptr<EventLoopThread>> threads_;
  // 所有 IO 线程的事件循环对象
  std::vector<EventLoop*> loops_;
};

} // namespace jmuduo


#endif