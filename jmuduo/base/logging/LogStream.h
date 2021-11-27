#ifndef _JMUDUO_BASE_LOG_STREAM_H_
#define _JMUDUO_BASE_LOG_STREAM_H_

#include <string.h>
#include <assert.h>

#include <string>

#include "noncopyable.h"

namespace jmuduo {

namespace detail {

const int kSmallBuffer = 4000; // 小型缓冲区大小，4K 字节
const int kLargeBuffer = 4000 * 1000; // 大型缓冲区大小，4M 字节

/**
 * @brief 固定大小的缓冲区
 *
 * @tparam SIZE 缓冲区大小（字节），非类型模版参数必须是一个常量表达式
 */
template <int SIZE>
class FixedBuffer : noncopyable {
 public:
  FixedBuffer() : cur_(data_) { setCookie(cookieStart); }

  ~FixedBuffer() { setCookie(cookieEnd); }

  /**
   * @brief 向缓冲区中写入长度为 len 的字符串数据 buf
   */
  void append(const char* buf, int len) {
    if (avail() > len) {
      memcpy(cur_, buf, len);
      cur_ += len;
    }
  }

  const char* data() const { return data_; }
  int length() const { return cur_ - data_; }

  char* current() { return cur_; }
  // 剩余空间大小
  int avail() const { return static_cast<int>(end() - data_); }
  // 移动空闲指针
  void add(size_t len) { cur_ += len; }

  // 重设缓冲区空闲指针，不会清空缓冲区
  void reset() { cur_ = data_; }
  // 清空缓冲区
  void bzero() { ::bzero(data_, sizeof data_); }

  // for used by GDB
  const char* debugString();
  void setCookie(void(*cookie)()) { cookie_ = cookie; }
  // for used by unit test，用 string 返回缓冲区内数据
  std::string asString() const { return std::string(data_, length()); }

 private:
  const char* end() const { return data_ + sizeof data_; }
  static void cookieStart();
  static void cookieEnd();

  void (*cookie_)();
  char data_[SIZE];  // 缓冲区
  char* cur_;        // 指向缓冲区中第一个空闲字节
};

}  // namespace detail

/**
 * @brief 对于固定长度的字符串，用该帮助类向 LogStream 输出可以节省 strlen 调用
 */
struct FixedString {
  FixedString(const char* str, int len) : str_(str), len_(len) {
    assert(strlen(str) == len_); // 线上版本 assert 不起作用，就不会调用 strlen
  }

  const char* str_;
  const size_t len_;
};

/**
 * @brief 日志流，封装用流的方式操作固定大小的缓冲区
 */
class LogStream : noncopyable {
 public:
  using Buffer = detail::FixedBuffer<detail::kSmallBuffer>;

  /* 定义各基本类型的输出函数 */

  LogStream& operator<<(bool v) { // bool 类型转换为 01 字符
    buffer_.append(v ? "1" : "0", 1);
    return *this;
  }

  LogStream& operator<<(short);
  LogStream& operator<<(unsigned short);
  LogStream& operator<<(int);
  LogStream& operator<<(unsigned int);
  LogStream& operator<<(long);
  LogStream& operator<<(unsigned long);
  LogStream& operator<<(long long);
  LogStream& operator<<(unsigned long long);
  // 指针类型，输出其指向的地址
  LogStream& operator<<(const void*);

  LogStream& operator<<(float v) {
    // 调用 double 类型的重载
    *this << static_cast<double>(v);
    return *this;
  }
  LogStream& operator<<(double);
  // LogStream& operator<<(long double);

  LogStream& operator<<(char v) {
    buffer_.append(&v, 1);
    return *this;
  }

  // LogStream& operator<<(signed char);
  // LogStream& operator<<(unsigned char);

  /* 定义 字符串 的输出函数 */
  // 防止被上面 const void* 类型的重载覆盖
  LogStream& operator<<(const char* v) {
    buffer_.append(v, strlen(v));
    return *this;
  }

  LogStream& operator<<(const FixedString& v) {
    buffer_.append(v.str_, v.len_);
    return *this;
  }

  // FIXME use StringPiece
  LogStream& operator<<(const std::string& v) {
    buffer_.append(v.c_str(), v.size());
    return *this;
  }

  // 写缓冲区
  void append(const char* data, int len) { buffer_.append(data, len); }
  const Buffer& buffer() const { return buffer_; }
  // 重置缓冲区空闲指针
  void resetBuffer() { buffer_.reset(); }

 private:
  void staticCheck(); // 检查本平台的算术类型长度

  // 将一个整数或浮点数转换为字符串输入到缓冲区
  template<typename T> 
  void formatInteger(T);

  Buffer buffer_;  // 日志流的缓冲区

  static const int kMaxNumericSize = 32; // 能够输出的算术类型的最大位数
};

/**
 * @brief 算术类型（即整数类型或浮点类型）的格式化帮助类；
 * LogStream 支持直接输出算数类型，但如果要按照某种格式输出，就需要用到该类
 */
class Fmt {
 public:
  template <typename T>
  Fmt(const char* fmt, T val);

  const char* data() const { return buf_; }
  int length() const { return length_; }

 private:
  char buf_[32]; // n
  int length_; // 缓冲区有效字符大小
};

// 将 fmt 输出到日志流缓冲区中
inline LogStream& operator<<(LogStream& ls, const Fmt& fmt) {
  ls.append(fmt.data(), fmt.length());
  return ls;
}

}  // namespace jmuduo

#endif
