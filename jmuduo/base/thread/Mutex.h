#ifndef _JMUDUO_MUTEX_H_
#define _JMUDUO_MUTEX_H_

#include <pthread.h>
#include <stdlib.h>

#include <cassert>

#include "../noncopyable.h"
#include "Thread.h"

namespace jmuduo {

// 锁
class MutexLock : noncopyable {
  friend class MutexLockGuard;
  friend class Condition;

 public:
  MutexLock() : holder_(0) { pthread_mutex_init(&mutex_, NULL); }

  ~MutexLock() {
    assert(holder_ == 0);  // 不等于0表示没有正确解锁
    pthread_mutex_destroy(&mutex_);
  }

  bool isLockedByThisThread() { return holder_ == CurrentThread::tid(); }

 private:
  pthread_mutex_t mutex_;
  pid_t holder_;  // 该锁所在的线程 ID

  void lock() {  // 仅供 MutextLockGuard 调用
    pthread_mutex_lock(&mutex_);
    holder_ = CurrentThread::tid();
  }

  void unlock() {  // 仅供 MutextLockGuard 调用
    holder_ = 0;
    pthread_mutex_unlock(&mutex_);
  }

  pthread_mutex_t *getPthreadMutex() {  // 仅供 Condition 调用
    return &mutex_;
  }
};

// 块级作用域锁
class MutexLockGuard {
 public:
  // 在代码块开始时为 m 上锁
  explicit MutexLockGuard(MutexLock &m) : mutex_(m) { mutex_.lock(); }
  // 代码块结束时，自动调用 m 的析构函数解锁
  ~MutexLockGuard() { mutex_.unlock(); }

 private:
  MutexLock &mutex_;
};

/**
 * 如此使用会创建一个临时对象并马上销毁，不会锁住临界区 MutexLockGuard:
 * MutexLockGuard(mutex);
 *
 * 正确写法:
 * MutexLockGuard lock(mutex);
 *
 * 程序里出现第一种写法时在编译时报错
 */
#define MutexLockGuard(x) static_assert(false, "missing mutex guard var name")

}  // namespace jmuduo

#endif