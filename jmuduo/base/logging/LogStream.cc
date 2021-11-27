#include "./LogStream.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <assert.h>

namespace jmuduo {
namespace detail {

// 10进制数的字符
const char digits[] = "9876543210123456789";
const char* zero = digits + 9;
static_assert(sizeof(digits) == 20);

// 16进制数的字符
const char digitsHex[] = "0123456789abcdef";
static_assert(sizeof(digitsHex) == 17);

/**
 * @brief 将数 value 的十进制字符表示写入缓冲区 buf
 *
 * @tparam T 数 value 的类型
 * @return size_t 写入的字符串长度
 */
template <typename T>
size_t convert(char buf[], T value) {
  T i = value;
  char* p = buf;

  do {  // 从低位到高位解析每一位上的数字
    int lsd = i % 10;
    i /= 10;
    *p++ = zero[lsd];
  } while (i != 0);

  if (value < 0)  // 小于 0 的数要加上负号
    *p++ = '-';

  *p = '\0';
  std::reverse(buf, p);  // 当前输出为 "低位到高位 - \0"，需要反转
  return p - buf;
}

/**
 * @brief 按 16 进制输出指针指向的地址 value 到缓冲区 buf
 * uintptr_t 类型用来按值类型保存一个指针
 * https://stackoverflow.com/questions/1845482/what-is-uintptr-t-data-type
 *
 * @param value 指针
 *
 * @return size_t 写入的字符串长度
 */
size_t convertHex(char buf[], uintptr_t value) {
  uintptr_t i = value;
  char* p = buf;

  do {
    int lsd = i % 16;
    i /= 16;
    *p++ = digitsHex[lsd];
  } while (i != 0);

  *p = '\0';
  std::reverse(buf, p);

  return p - buf;
}

}  // namespace detail

}  // namespace jmuduo

using namespace jmuduo;
using namespace jmuduo::detail;

template <int SIZE>
const char* FixedBuffer<SIZE>::debugString() {
  *cur_ = '\0';
  return data_;
}

template <int SIZE>
void FixedBuffer<SIZE>::cookieStart() {}

template <int SIZE>
void FixedBuffer<SIZE>::cookieEnd() {}

template class FixedBuffer<kSmallBuffer>;
template class FixedBuffer<kLargeBuffer>;

void LogStream::staticCheck() {
  /**
   * https://stackoverflow.com/questions/747470/what-is-the-meaning-of-numeric-limitsdoubledigits10
   * numeric_limits::digits10 表示某种类型能精确表示的数字位数
   * 例如 numeric_limits<char>::digits10=2，因为类型 char 有 8 位，
   * 能精确表示两位数 0-99，但不能表示三位数 256-999
   */
  static_assert(kMaxNumericSize - 10 > std::numeric_limits<double>::digits10);
  static_assert(kMaxNumericSize - 10 >
                std::numeric_limits<long double>::digits10);
  static_assert(kMaxNumericSize - 10 > std::numeric_limits<long>::digits10);
  static_assert(kMaxNumericSize - 10 >
                std::numeric_limits<long long>::digits10);
}

template <typename T>
void LogStream::formatInteger(T v) {
  if (buffer_.avail() >= kMaxNumericSize) {
    size_t len = convert(buffer_.current(), v);
    buffer_.add(len);
  }
}

LogStream& LogStream::operator<<(short v) {
  *this << static_cast<int>(v);
  return *this;
}
LogStream& LogStream::operator<<(unsigned short v) {
  *this << static_cast<unsigned int>(v);
  return *this;
}
LogStream& LogStream::operator<<(int v) {
  formatInteger(v);
  return *this;
}
LogStream& LogStream::operator<<(unsigned int v) {
  formatInteger(v);
  return *this;
}
LogStream& LogStream::operator<<(long v) {
  formatInteger(v);
  return *this;
}
LogStream& LogStream::operator<<(unsigned long v) {
  formatInteger(v);
  return *this;
}
LogStream& LogStream::operator<<(long long v) {
  formatInteger(v);
  return *this;
}
LogStream& LogStream::operator<<(unsigned long long v) {
  formatInteger(v);
  return *this;
}
// 指针类型，输出其指向的地址
LogStream& LogStream::operator<<(const void* p) {
  uintptr_t v = reinterpret_cast<uintptr_t>(p);
  if (buffer_.avail() >= kMaxNumericSize) {
    char* buf = buffer_.current();
    buf[0] = '0';
    buf[1] = 'x';
    size_t len = convertHex(buf + 2, v);
    buffer_.add(len + 2);
  }

  return *this;
}
// FIXME 将浮点数转换为字符串应该使用 Grisu3 算法
// https://daily.zhihu.com/story/4509088
LogStream& LogStream::operator<<(double v) {
  if (buffer_.avail() >= kMaxNumericSize) {
    int len = snprintf(buffer_.current(), kMaxNumericSize, "%.12g", v);
    buffer_.add(len);
  }
  return *this;
}

/**
 * @brief 将算数类型 T 的值 val 按格式化字符串 fmt 的定义转化为字符串
 */
template<typename T>
Fmt::Fmt(const char* fmt, T val) {
  // T 必须是算术类型（即整数类型或浮点类型）
  static_assert(std::is_arithmetic<T>::value == true);
  length_ = snprintf(buf_, sizeof buf_, fmt, val);
  assert(static_cast<size_t>(length_) < sizeof buf_);
}

// 将模版显式实例化

template Fmt::Fmt(const char* fmt, char);

template Fmt::Fmt(const char* fmt, short);
template Fmt::Fmt(const char* fmt, unsigned short);
template Fmt::Fmt(const char* fmt, int);
template Fmt::Fmt(const char* fmt, unsigned int);
template Fmt::Fmt(const char* fmt, long);
template Fmt::Fmt(const char* fmt, unsigned long);
template Fmt::Fmt(const char* fmt, long long);
template Fmt::Fmt(const char* fmt, unsigned long long);

template Fmt::Fmt(const char* fmt, float);
template Fmt::Fmt(const char* fmt, double);