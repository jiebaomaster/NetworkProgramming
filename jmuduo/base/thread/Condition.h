#ifndef _JMUDUO_CONDITION_H_
#define _JMUDUO_CONDITION_H_

#include "Mutex.h"

namespace jmuduo
{
  
// 条件变量
class Condition {
 public:
  explicit Condition(MutexLock &m) : mutex_(m) {
    pthread_cond_init(&cond_, NULL);
  }
  ~Condition() { pthread_cond_destroy(&cond_); }

  void wait() { pthread_cond_wait(&cond_, mutex_.getPthreadMutex()); }
  bool waitForSeconds(int seconds) {  // 等待一段时间
    struct timespec abstime;
    clock_gettime(CLOCK_REALTIME, &abstime);
    abstime.tv_sec += seconds;
    return ETIMEDOUT ==
           pthread_cond_timedwait(&cond_, mutex_.getPthreadMutex(), &abstime);
  }
  void notify() { pthread_cond_signal(&cond_); }
  void notifyAll() { pthread_cond_broadcast(&cond_); }

 private:
  MutexLock &mutex_;
  pthread_cond_t cond_;
};

} // namespace jmuduo



#endif