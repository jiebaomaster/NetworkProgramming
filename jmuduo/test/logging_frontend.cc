#include "../base/logging/Logging.h"

int main() {
  { LOG_TRACE << "log trace"; }
  { LOG_DEBUG << "log debug"; }
  { LOG_INFO << "log info"; }
  { LOG_WARN << "log warning"; }
  { LOG_ERROR << "log erroe"; }
  { LOG_SYSERR << "log system error"; }
  {
    LOG_FATAL << "log fatal";
    LOG_SYSFATAL << "log system fatal";
  }
}