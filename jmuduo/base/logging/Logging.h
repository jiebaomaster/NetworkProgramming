#ifndef _JMUDUO_BASE_LOGGING_H_
#define _JMUDUO_BASE_LOGGING_H_

#include "../datetime/Timestamp.h"
#include "./LogStream.h"

namespace jmuduo {

/**
 * 日志前端，代表一条日志。每条日志占一行，格式格式如下：
 * 日期      时间     微秒     线程id  级别  (函数名) 正文 - 源文件名:行号\n
 * YYYYMMDD HH:mm:ss.ssssssZ 123456 DEBUG main hello - text.cc:51\n 
 * 用户在写日志时在栈上构建一个 Logger 临时对象，构建时向缓冲区写入正文前的部分，
 * 之后的正文也写入缓冲区，直到临时对象析构时才通过全局日志输出函数将缓冲区的内容写入到日志后端
 */
class Logger {
 public:
  /**
   * 日志输出级别
   * 1. TRACE、DEBUG、INFO 根据当前日志级别选择性打印
   * 2. 只有 TRACE 和 DEBUG 会记录本条日志所在的函数名
   * 3. FATAL 需要停止程序运行
   */
  enum LogLevel {
    TRACE,
    DEBUG, // 默认级别
    INFO,
    WARN,  // 警告
    ERROR, // 错误
    FATAL, // 致命错误
    NUM_LOG_LEVELS,
  };

  Logger(const char* file, int line);
  Logger(const char* file, int line, LogLevel level);
  Logger(const char* file, int line, LogLevel level, const char* func);
  Logger(const char* file, int line, bool toAbort);
  ~Logger();

  LogStream& stream() { return impl_.stream_; }

  // 返回全局日志输出级别
  static LogLevel logLevel();
  // 设置全局日志输出级别
  static void setLogLevel(LogLevel level);

  /* 日志前端用来操作日志后端的接口函数类型 */
  // 全局日志输出函数应该把一条长为 len 的日志 msg 写入到日志后端
  using OutputFunc = void (*)(const char* msg, int len);
  // 全局日志刷新函数应该使日志后端的缓存全部写入到文件中
  using FlushFunc = void (*)();
  // 设置全局写日志后端接口函数
  static void setOutput(OutputFunc);
  // 设置全局刷新日志后端缓冲区接口函数
  static void setFlush(FlushFunc);

 private:
  class Impl {
   public:
    using LogLevel = Logger::LogLevel;
    Impl(LogLevel level, int old_errno, const char* file, int line);
    void formatTime();  // log 时间
    void finish();  // log 源文件名，代码行数

    Timestamp time_; // 本条 log 的时间
    LogStream stream_; // 本条 log 使用的日志操作流
    LogLevel level_; // 本条 log 的日志级别
    int line_; // 本条 log 的代码行数
    const char* fullname_; // 源文件全路径名
    const char* basename_; // 源文件名
  };

  Impl impl_;
};


/* 用户直接使用的日志输出宏 */
// TRACE、DEBUG、INFO 三种信息采用选择性打印的方式
// 只有 TRACE 和 DEBUG 会记录本条日志所在的函数名
#define LOG_TRACE                                          \
  if (jmuduo::Logger::logLevel() <= jmuduo::Logger::TRACE) \
  jmuduo::Logger(__FILE__, __LINE__, jmuduo::Logger::TRACE, __func__).stream()
#define LOG_DEBUG                                          \
  if (jmuduo::Logger::logLevel() <= jmuduo::Logger::DEBUG) \
  jmuduo::Logger(__FILE__, __LINE__, jmuduo::Logger::DEBUG, __func__).stream()
#define LOG_INFO                                          \
  if (jmuduo::Logger::logLevel() <= jmuduo::Logger::INFO) \
  jmuduo::Logger(__FILE__, __LINE__).stream()
#define LOG_WARN jmuduo::Logger(__FILE__, __LINE__, jmuduo::Logger::WARN).stream()
#define LOG_ERROR jmuduo::Logger(__FILE__, __LINE__, jmuduo::Logger::ERROR).stream()
#define LOG_FATAL jmuduo::Logger(__FILE__, __LINE__, jmuduo::Logger::FATAL).stream()
// 存在系统错误时需要输出 errno
#define LOG_SYSERR jmuduo::Logger(__FILE__, __LINE__, false).stream()
#define LOG_SYSFATAL jmuduo::Logger(__FILE__, __LINE__, true).stream()

// 根据系统调用返回的错误号 savedErrno 解析字符串形式的错误信息
const char* strerror_tl(int savedErrno);

// Taken from glog/logging.h
//
// Check that the input is non NULL.  This very useful in constructor
// initializer lists.

#define CHECK_NOTNULL(val) \
  ::jmuduo::CheckNotNull(__FILE__, __LINE__, "'" #val "' Must be non Null", (val))
// A small helper for CHECK_NOTNULL().
template <typename T>
T* CheckNotNull(const char *file, int line, const char* names, T* ptr) {
  if(ptr == nullptr)
    Logger(file, line, Logger::FATAL).stream() << names;

  return ptr;
}


// https://blog.csdn.net/xiaoc_fantasy/article/details/79570788
// upcast 派生类=>基类
template<typename To, typename From>
inline To implicit_cat(From const &f) {
  return f;
}

}  // namespace jmuduo

#endif