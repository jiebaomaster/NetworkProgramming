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
 * @brief 传递给 TcpServer，在 TCP socket 连接建立后和连接断开后回调
 *        可用 TcpConnection::connected() 判断是建立还是断开
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
 * @brief 内部使用，在 TCP socket 被关闭时调用
 * @param TcpConnectionPtr 指向被关闭的 TCP 连接
 */
using CloseCallback = std::function<void (const TcpConnectionPtr&)>;

/**
 * 非阻塞网络编程的发送数据比读取数据要困难得多
 * 1. 编码困难。只在输出缓冲区有数据待写时才关注 writable 事件，一旦写完就要取消关注
 * 2. 设计和安全性困难。如果发送数据的速度高于对方接受的数据，会造成数据在本地内存中堆积
 * 故需要下面两个回调控制缓冲区的大小
 */
/**
 * @brief 传递给 TcpServer，对于某一连接，每次发送缓冲区被清空时调用
 * @param TcpConnectionPtr 指向 TCP 连接
 */ 
using WriteCompleteCallback = std::function<void (const TcpConnectionPtr&)>;
/**
 * @brief 传递给 TcpServer，对于某一连接，每次发送缓冲区的长度超过用户指定大小时回调（只在上升沿触发一次）
 * @param TcpConnectionPtr 指向 TCP 连接
 * @param bufSize 当前发送缓冲区大小
 */ 
using HighWaterMarkCallback = std::function<void (const TcpConnectionPtr&, size_t bufSize)>;
}  // namespace jmuduo

#endif