#ifndef _JMUDUO_TIMESTAMP_H_
#define _JMUDUO_TIMESTAMP_H_

#include <stdint.h>

#include <string>

#include "../noncopyable.h"

namespace jmuduo {

/**
 * @brief 时间戳，分辨率为微秒（μs），1s = 1000*1000μs。
 * 表示自 1970-01-01T00:00:00Z 以来的微秒数
 *
 * This class is immutable.
 * It's recommended to pass it by value, since it's passed in register on x64.
 */
class Timestamp : copyable {
 public:
  Timestamp();
  /**
   * @brief 构造一个特定时间的时间戳
   *
   * @param microSecondsSinceEpoch 自 1970-01-01T00:00:00Z 以来的微秒数
   */
  explicit Timestamp(int64_t microSecondsSinceEpoch);

  // 交换两个时间戳
  void swap(Timestamp& that) {
    std::swap(microSecondsSinceEpoch_, that.microSecondsSinceEpoch_);
  }

  // 使用默认的 拷贝/赋值/删除

  // 返回表示时间戳的字符串 "seconds.microSeconds"
  std::string toString() const;
  // 返回表示时间戳的 ISO 格式化字符串
  std::string toFormattedString() const;

  // 判断时间戳是是否合法
  bool valid() const { return microSecondsSinceEpoch_ > 0; }

  // 返回内部时间戳，应该只在库的内部使用
  int64_t microSecondsSinceEpoch() const { return microSecondsSinceEpoch_; }

  // 返回当前时间的时间戳
  static Timestamp now();
  // 返回一个不合法的时间戳
  static Timestamp invalid();

  // 每秒的微秒数
  static const int kMicroSecondsPerSecond = 1000 * 1000;

 private:
  // 自 1970-01-01T00:00:00Z 以来的微秒数
  int64_t microSecondsSinceEpoch_;
};

inline bool operator<(Timestamp lhs, Timestamp rhs) {
  return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
}

inline bool operator==(Timestamp lhs, Timestamp rhs) {
  return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
}

/**
 * @brief 返回两个时间戳的差值
 *
 * @return double 时间的差值，精度为“秒”，
 * double 的精度为52位，足够在未来100年内维持一微秒的分辨率
 */
inline double timeDifference(Timestamp high, Timestamp low) {
  int64_t diff = high.microSecondsSinceEpoch() - low.microSecondsSinceEpoch();
  return static_cast<double>(diff) / Timestamp::kMicroSecondsPerSecond;
}

/**
 * @brief 在给定时间戳 timestamp 上添加 seconds 秒的时间
 *
 * @return Timestamp(timestamp + seconds)
 */
inline Timestamp addTime(Timestamp timestamp, double seconds) {
  int64_t delta =
      static_cast<int64_t>(seconds * Timestamp::kMicroSecondsPerSecond);
  return Timestamp(timestamp.microSecondsSinceEpoch() + delta);
}

}  // namespace jmuduo

#endif