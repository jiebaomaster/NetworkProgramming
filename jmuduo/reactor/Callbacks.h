#ifndef _JMUDUO_CALLBACKS_H_
#define _JMUDUO_CALLBACKS_H_

#include <functional>
#include <memory>

#include "../base/datetime/Timestamp.h"

namespace jmuduo {

class Buffer;
class TcpConnection;

/* 所有客户可能会用到的回调函数形式定义 */

// 定时器回调函数
using TimerCallback = std::function<void()>;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

/**
 * @brief 传递给 TcpServer，在 TCP socket 建立后回调
 * @param TcpConnectionPtr 指向建立的 TCP 连接
 */
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
/**
 * @brief 传递给 TcpServer，在 TCP socket 有消息可读时回调
 * @param TcpConnectionPtr 指向可读的 TCP 连接
 * @param buf 读到的消息
 * @param receiveTime 事件发生的时间，即 poll(2) 返回的时间
 */
using MessageCallback =
    std::function<void(const TcpConnectionPtr&, Buffer* buf, Timestamp receiveTime)>;

/**
 * @brief 传递给 TcpServer，在 TCP socket 被关闭时调用
 * @param TcpConnectionPtr 指向被关闭的 TCP 连接
 */
using CloseCallback = std::function<void (const TcpConnectionPtr&)>;
}  // namespace jmuduo

#endif