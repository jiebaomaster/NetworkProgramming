#include "Logging.h"

#include <assert.h>
#include <stdio.h>

#include "../thread/Thread.h"

namespace jmuduo {

__thread char t_errnobuf[512];  // 存储系统调用错误信息的缓冲区
__thread char t_time[32];  // 用于格式化时间的缓冲区，per thread 保证了线程安全
__thread time_t t_lastSecond;  // 最后一次记录日志的时间

// 解析系统调用错误
const char* strerror_tl(int savedErrno) {
  return strerror_r(savedErrno, t_errnobuf, sizeof t_errnobuf);
}

// 初始化日志输出级别
Logger::LogLevel initLogLevel() {
  // 日志输出级别的初始化依赖环境变量，而不是条件编译，这样如果要改变初始的输出级别，
  // 只需要修改环境变量，然后重新运行程序即可，而不需要重新编译
  if (::getenv("JMUDUO_LOG_TRACE"))  // 读环境变量
    return Logger::TRACE;
  else  // 默认
    return Logger::DEBUG;
}

const char* LogLevelName[Logger::NUM_LOG_LEVELS] = {
    "TRACE ", "DEBUG ", "INFO  ", "WARN  ", "ERROR ", "FATAL ",
};

void defaultOutput(const char* msg, int len) {
  // 写入到标准输出
  size_t n = fwrite(msg, 1, len, stdout);
  // FIXME 检查 n 的大小判断是否完全写入
}

void defaultFlush() { 
  fflush(stdout);  // 刷新标准输出
}

// 全局统一日志输出级别，一个进程中所有线程的日志输出级别相同
Logger::LogLevel g_logLevel = initLogLevel();
/* TODO 默认的日志后端为 stdout，为了更好的性能应该支持异步日志后端 */
// 全局统一写日志后端接口
Logger::OutputFunc g_output = defaultOutput;
// 全局统一刷新日志后端缓冲区接口
Logger::FlushFunc g_flush = defaultFlush;

}  // namespace jmuduo

using namespace jmuduo;

Logger::Impl::Impl(LogLevel level, int savedErrno, const char* file, int line)
    : time_(Timestamp::now()),
      stream_(),
      level_(level),
      line_(line),
      fullname_(file),
      basename_(nullptr) {
  // 从全路径名中解析源文件名
  const char* path_sep_pos = strrchr(fullname_, '/'); // 最后一个 '/' 出现的位置
  basename_ = (path_sep_pos != nullptr) ? path_sep_pos + 1 : fullname_;

  formatTime(); // log 时间
  Fmt tid("%6d ", CurrentThread::tid()); // 格式化输出 tid
  assert(tid.length() == 7); // 每个平台的 pid 长度可能不同
  stream_ << FixedString(tid.data(), 7) // log 线程 id
          << FixedString(LogLevelName[level], 6); // log 当前日志对象的输出级别
  if (savedErrno != 0)  // （如果有）log 系统调用错误
    stream_ << strerror_tl(savedErrno) << " (error=" << savedErrno << ") ";
}

void Logger::Impl::formatTime() {
  int64_t microSecondsSinceEpoch = time_.microSecondsSinceEpoch();
  time_t seconds = static_cast<time_t>(microSecondsSinceEpoch / 1000000);
  int microseconds = static_cast<int>(microSecondsSinceEpoch % 1000000);
  if (seconds != t_lastSecond) {
    t_lastSecond = seconds;
    struct tm tm_time;
    ::gmtime_r(&seconds, &tm_time);  // FIXME TimeZone::fromUtcTime

    // 这里所用的缓冲区 t_time 是 per thread 的，保证了线程安全
    int len =
        snprintf(t_time, sizeof(t_time), "%4d-%02d-%02d %02d:%02d:%02d",
                 tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                 tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
    assert(len == 19);
  }
  Fmt us(".%06dZ ", microseconds);
  assert(us.length() == 9);
  stream_ << FixedString(t_time, 19) << FixedString(us.data(), 9); // log 时间
}

void Logger::Impl::finish() {
  stream_ << " - " << basename_ << ':' << line_ << '\n'; // log 文件名，代码行数
}

Logger::Logger(const char* file, int line) : impl_(INFO, 0, file, line) {}

Logger::Logger(const char* file, int line, LogLevel level)
    : impl_(level, 0, file, line) {}

Logger::Logger(const char* file, int line, LogLevel level, const char* func)
    : impl_(level, 0, file, line) {
  impl_.stream_ << func << ' '; // log 函数名
}

Logger::Logger(const char* file, int line, bool toAbort)
    : impl_(toAbort ? FATAL : ERROR, errno, file, line) {}

Logger::~Logger() {
  impl_.finish();
  const LogStream::Buffer& buf(stream().buffer());
  // 析构时才通过全局日志输出函数写入到日志后端
  g_output(buf.data(), buf.length());
  // 如果当前记录的是致命错误，则需要停止系统运行
  if (impl_.level_ == FATAL) {
    g_flush(); // 保证系统停止运行前将日志后端的缓存全部写入到文件中
    abort(); 
  }
}

Logger::LogLevel Logger::logLevel() { return g_logLevel; }

void Logger::setLogLevel(Logger::LogLevel level) { g_logLevel = level; }

void Logger::setOutput(OutputFunc out) { g_output = out; }

void Logger::setFlush(FlushFunc flush) { g_flush = flush; }