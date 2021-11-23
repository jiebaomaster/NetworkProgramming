#include "Timestamp.h"

#include <inttypes.h>
#include <stdio.h>
#include <sys/time.h>
#include <assert.h>

using namespace jmuduo;
using std::string;

static_assert(sizeof(Timestamp) == sizeof(int64_t));

Timestamp::Timestamp() : microSecondsSinceEpoch_(0) {}

Timestamp::Timestamp(int64_t microSecondsSinceEpoch)
    : microSecondsSinceEpoch_(microSecondsSinceEpoch) {}

string Timestamp::toString() const {
  char buf[32] = {0};
  int64_t seconds = microSecondsSinceEpoch_ / kMicroSecondsPerSecond;
  int64_t microSeconds = microSecondsSinceEpoch_ % kMicroSecondsPerSecond;
  snprintf(buf, sizeof(buf) - 1, "%" PRId64 ".%06" PRId64 "", seconds,
           microSeconds);

  return buf;
}

string Timestamp::toFormattedString() const {
  char buf[64] = {0};
  time_t seconds =
      static_cast<time_t>(microSecondsSinceEpoch_ / kMicroSecondsPerSecond);
  int microSeconds =
      static_cast<int>(microSecondsSinceEpoch_ % kMicroSecondsPerSecond);
  struct tm tm_time;
  gmtime_r(&seconds, &tm_time); // 用 tm_time 返回 seconds 指定的 utc 时间

  snprintf(buf, sizeof(buf) - 1, "%4d-%02d-%02d %02d:%02d:%02d.%06dZ",
           tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
           tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec, microSeconds);

  return buf;
}

Timestamp Timestamp::now() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return Timestamp(tv.tv_sec * kMicroSecondsPerSecond + tv.tv_usec);
}

Timestamp Timestamp::invalid() { return Timestamp(); }
