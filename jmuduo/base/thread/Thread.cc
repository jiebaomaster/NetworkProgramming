#include "Thread.h"

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <sys/prctl.h>
#include <linux/unistd.h>

namespace { // 只在本文件中使用的名字

// 线程独立变量，保存当前线程的线程 ID，不用每次都使用系统调用获取
__thread pid_t t_cachedTid = 0;
__thread const char * t_threadName = "unnamedThread";

/**
 * @brief 手动包装返回线程 ID 的系统调用
 */
pid_t gettid_() { return static_cast<pid_t>(::syscall(SYS_gettid)); }

/**
 * @brief 传递给线程执行函数的参数
 */
struct ThreadData {
  using ThreadFunc = jmuduo::Thread::ThreadFunc;
  
  ThreadFunc func_;
  std::string name_;
  std::weak_ptr<pid_t> wkTid_;

  ThreadData(const ThreadFunc &func, const std::string &name,
             const std::shared_ptr<pid_t> &tid)
      : func_(func), name_(name), wkTid_(tid) {}

  /**
   * 执行线程的 worker 函数，首先要获取线程 id 和修改线程名字
   */
  void runInThread() {
    /**
     * @brief 有三个地方会保存tid：
     * 本文件的线程局部变量，代表本线程的对象Thread，属于线程的事件循环对象 EventLoop
     * 1. 创建 Thread，此时 tid=0，开始执行线程 startThread 函数
     * 2.（本函数中）真正执行 worker 函数前，调用系统调用获取 tid，设置 t_cachedTid，
     *    并修改 Thread.tid_，此时有可能 Thread 在别的线程中被销毁了，所以这里使用
     *    weak_ptr 尝试提升，确保存在后再修改
     * 3. 在线程的 worker 函数中创建 EventLoop，且用缓存的 tid 初始化 EventLoop.tid_
     */
    pid_t tid = jmuduo::CurrentThread::tid(); // 第一次获取 tid
    std::shared_ptr<pid_t> ptid = wkTid_.lock();
    if (ptid) { // 如果 Thread 对象存在
      *ptid = tid; // 修改 Thread.tid_
      ptid.reset();
    }

    // 修改线程的名字
    if (!name_.empty()) t_threadName = name_.c_str();
    ::prctl(PR_SET_NAME, t_threadName);
    // 执行线程 worker 函数
    func_();

    t_threadName = "finished";
  }
};

/**
 * @brief 由 pthread_create 调用，包装
 * 
 * @param obj 
 * @return void* 
 */
void *startThread(void *obj) {
  ThreadData* data = static_cast<ThreadData*>(obj); // 解析线程执行函数的参数
  data->runInThread(); // 执行
  delete data; // worker 函数执行完毕之后要释放参数的内存空间
  return NULL;
}

}  // namespace

using namespace jmuduo;

pid_t CurrentThread::tid() {
  if (t_cachedTid == 0) t_cachedTid = gettid_();

  return t_cachedTid;
}

bool CurrentThread::isMainThread() { 
  // 在一个进程中，只有主线程的线程 id 和进程 id 是一样的
  return ::getpid() == tid();
}

AtomicInt32 Thread::numCreated_;

Thread::Thread(const ThreadFunc &func, const std::string &name)
    : started_(false),
      joined_(false),
      pthreadId_(0),
      tid_(new pid_t(0)),
      func_(func),
      name_(name) {
  numCreated_.increment();  // 记录一次线程创建
}

Thread::~Thread() {
  // 析构时如果线程还未执行完毕，设置主线程不必等待该子线程
  if (started_ && !joined_)
    pthread_detach(pthreadId_);
}

void Thread::start() {
  assert(!started_);  // 还未开始的线程才能开始

  started_ = true;  // 标志线程开始运行
  ThreadData *data = new ThreadData(func_, name_, tid_);
  if (pthread_create(&pthreadId_, NULL, startThread, data)) {
    // 如果创建失败
    started_ = false;
    delete data;
    abort();
  }
}

void Thread::join() {
  assert(started_);
  assert(!joined_);
  
  joined_ = true; // 标志线程执行结束
  pthread_join(pthreadId_, NULL);
}

