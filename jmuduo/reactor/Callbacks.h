#ifndef _JMUDUO_CALLBACKS_H_
#define _JMUDUO_CALLBACKS_H_

#include <functional>
#include <memory>

#include "datetime/Timestamp.h"

namespace jmuduo {

/* 所有客户可能会用到的回调函数形式定义 */

// 定时器回调函数
using TimerCallback = std::function<void()>;

class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

/**
 * @brief 传递给 TcpServer，在 TCP socket 建立后回调
 * @param TcpConnectionPtr 指向建立的 TCP 连接
 */
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
/**
 * @brief 传递给 TcpServer，在 TCP socket 有消息可读时回调
 * @param TcpConnectionPtr 指向可读的 TCP 连接
 * @param data 读到的消息
 * @param len 消息长度
 */
using MessageCallback =
    std::function<void(const TcpConnectionPtr&, const char* data, ssize_t len)>;
}  // namespace jmuduo

#endif