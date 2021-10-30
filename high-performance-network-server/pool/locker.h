#ifndef _LOCKER_H_
#define _LOCKER_H_

/**
 * @file locker.h
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2021-10-26
 *
 * @copyright Copyright (c) 2021
 *
 * 封装 pthread 提供的同步操作
 *
 */

#include <pthread.h>
#include <semaphore.h>

#include <exception>

// 信号量
class sem {
 public:
  //创建并初始化信号量
  sem() {
    if (sem_init(&m_sem, 0, 0) != 0) throw std::exception();
  }
  // 销毁信号量
  ~sem() { sem_destroy(&m_sem); }

  // 等待信号量
  bool wait() { return sem_wait(&m_sem) == 0; }

  // 增加信号量
  bool post() { return sem_post(&m_sem) == 0; }

 private:
  sem_t m_sem;
};

// 互斥锁
class locker {
 public:
  // 创建并初始化锁
  locker() {
    if (pthread_mutex_init(&m_mutex, nullptr) != 0) throw std::exception();
  }
  // 销毁锁
  ~locker() { pthread_mutex_destroy(&m_mutex); }
  // 获取互斥锁
  bool lock() { return pthread_mutex_lock(&m_mutex) == 0; }
  // 释放互斥锁
  bool unlock() { return pthread_mutex_unlock(&m_mutex) == 0; }

 private:
  pthread_mutex_t m_mutex;
};

// 条件变量
class cond {
 public:
  // 创建并初始化条件变量
  cond() {
    if (pthread_mutex_init(&m_mutex, nullptr) != 0) throw std::exception();
    if (pthread_cond_init(&m_cond, nullptr) != 0) {
      // 构造函数中一旦出问题，就应该立即释放已成功分配的资源
      pthread_mutex_destroy(&m_mutex);
      throw std::exception();
    }
  }

  // 销毁条件变量
  ~cond() {
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
  }

  // 等待条件变量
  bool wait() {
    int ret = 0;
    pthread_mutex_lock(&m_mutex);
    ret = pthread_cond_wait(&m_cond, &m_mutex);
    pthread_mutex_unlock(&m_mutex);
    return ret == 0;
  }

  // 唤醒等待条件变量的线程
  bool signal() { return pthread_cond_signal(&m_cond) == 0; }

 private:
  pthread_mutex_t m_mutex;
  pthread_cond_t m_cond;
};

#endif