#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

#include <pthread.h>

#include <cstdio>
#include <exception>
#include <list>

#include "../locker.h"

/**
 * @brief 线程池
 *
 * @tparam T 任务类型
 */
template <typename T>
class threadpool {
 public:
  threadpool(int thread_number = 8, int max_requests = 10000);
  ~threadpool();
  /**
   * @brief 往请求队列中添加任务
   *
   * @param request
   * @return true
   * @return false
   */
  bool append(T* request);

 private:
  /**
   * @brief 工作线程运行的函数，不断从任务队列中取出任务并运行
   * 
   * @param arg
   * @return void*
   */
  static void* worker(void* arg);
  /**
   * @brief 开始运行
   *
   */
  void run();

 private:
  int m_thread_number;  // 线程池中的线程数
  int m_max_requests;   // 请求队列中允许的最大请求数
  pthread_t* m_threads;  // 描述线程次的数组，数组大小为 m_thread_number
  std::list<T*> m_workqueue;  // 请求队列
  locker m_queuelocker;       // 保护请求队列的互斥锁
  sem m_queuestat;            // 是否有任务需要处理
  bool m_stop;                // 是否结束线程
};

template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests)
    : m_thread_number(thread_number),
      m_max_requests(m_max_requests),
      m_threads(nullptr),
      m_stop(false) {
  if (thread_number <= 0 || max_requests <= 0) throw std::exception();

  m_threads = new pthread_t[m_thread_number];
  if (!m_threads) throw std::exception();

  // 创建 m_thread_number 个线程，并把他们都设置为脱离线程，脱离线程推出时自行释放其占有的系统资源
  for (int i = 0; i < m_thread_number; i++) {
    printf("create the %dth thread\n", i);
    if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
      delete[] m_threads;
      throw std::exception();
    }

    if (pthread_detach(*(m_threads + i)) != 0) {
      delete[] m_threads;
      throw std::exception();
    }
  }
}

template <typename T>
threadpool<T>::~threadpool() {
  delete[] m_threads;
  m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T *request) {
  m_queuelocker.lock();
  if(m_workqueue.size() > m_max_requests) {
    m_queuelocker.unlock();
    return false;
  }
  m_workqueue.push_back(request); // 添加任务到请求队列
  m_queuelocker.unlock();
  m_queuestat.post(); // 增加信号
  return true;
}

template <typename T>
void * threadpool<T>::worker(void *arg)  {
  threadpool* pool = (threadpool*)arg;
  pool->run();
  return pool;
}

template <typename T>
void threadpool<T>::run() {
  while(!m_stop) {
    m_queuestat.wait(); // 等待信号量
    m_queuelocker.lock();
    if(m_workqueue.empty()) {
      m_queuelocker.unlock();
      continue;
    }

    // 取出队头的请求对象
    T *request = m_workqueue.front();
    m_workqueue.pop_front();
    m_queuelocker.unlock();
    if(!request)
      continue;
    // 运行请求对象的处理方法
    request->process();
  }
}

#endif