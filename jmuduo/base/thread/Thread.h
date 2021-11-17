#ifndef _JMUDUO_THREAD_H_
#define _JMUDUO_THREAD_H_

#include <pthread.h>
#include <sys/types.h>

#include <functional>
#include <memory>

#include "../noncopyable.h"
#include "Atomic.h"

namespace jmuduo {

/**
 * pthread 线程 API 的面向对象包装
 */
class Thread : noncopyable {
 public:
  // 定义线程执行的函数类型
  using ThreadFunc = std::function<void()>;

  explicit Thread(const ThreadFunc&, const std::string& name = std::string());
  ~Thread();

  // 线程开始执行
  void start();
  // 等待线程执行结束
  void join();

  bool started() const { return started_; }
  pid_t tid() const { return *tid_; }
  const std::string& name() const { return name_; }

  static int numCreated() { return numCreated_.get(); }

 private:
  bool started_;                // 线程是否已启动
  bool joined_;                 // 线程是否已结束
  pthread_t pthreadId_;         // 线程标志符
  std::shared_ptr<pid_t> tid_;  // 线程 ID
  ThreadFunc func_;             // 线程执行的函数
  std::string name_;            // 线程的名字

  static AtomicInt32 numCreated_;  // 记录总共创建的线程数
};

namespace CurrentThread {

// 获取线程 ID
pid_t tid();
// 判断当前线程是否是主线程
bool isMainThread();

}

}  // namespace jmuduo

#endif