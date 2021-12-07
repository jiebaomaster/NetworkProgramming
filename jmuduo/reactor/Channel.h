#ifndef _JMUDUO_CHANNEL_H_
#define _JMUDUO_CHANNEL_H_

#include "noncopyable.h"
#include "../base/datetime/Timestamp.h"

#include <functional>

namespace jmuduo {

class EventLoop;

/**
 * @brief 可被事件监听的 IO 信道
 * 该对象不拥有文件描述符。文件描述符指：socket、eventfd、timefd、signalfd
 * 用户一般不直接使用 Channel，应使用更上层的封装，如 TcpConnection
 */
class Channel : noncopyable {
 public:
  // 定义事件回调函数的类型
  using EventCallback = std::function<void()>;
  // 定义可读事件事件回调函数的类型
  using ReadEventCallback = std::function<void(Timestamp)>;

  Channel(EventLoop*, int fd);
  ~Channel();

  /**
   * @brief 解析 revents_，使用相应的回调函数处理事件
   * @param receiveTime 事件发生的时间，即 poll(2) 返回的时间
   */
  void handleEvent(Timestamp receiveTime);

  /* 设置各类型事件的回调函数，由使用信道的类调用 */
  void setReadCallback(const ReadEventCallback& cb) { readCallback_ = cb; };
  void setWriteCallback(const EventCallback& cb) { writeCallback_ = cb; };
  void setErrorCallback(const EventCallback& cb) { errorCallback_ = cb; };
  void setCloseCallback(const EventCallback& cb) { closeCallback_ = cb; };

  int fd() const { return fd_; }
  int events() const { return events_; }
  void set_revents(int revents) { revents_ = revents; }
  // 判断信道是否不监听任何事件
  bool isNoneEvent() const { return events_ == kNoneEvent; }

  /* 设置该信道监听的事件 */
  void enableReading() {
    events_ |= kReadEvent;
    update();
  }
  void enableWriting() {
    events_ |= kWriteEvent;
    update();
  }
  void disableWriting() {
    events_ &= ~kWriteEvent;
    update();
  }
  void disableAll() {
    events_ = kNoneEvent;
    update();
  }

  int index() { return index_; }
  void set_index(int idx) { index_ = idx; }

  EventLoop* ownerLoop() { return loop_; }

 private:
  // 该信道注册的事件发生变化，去 EventLoop 的 poller 中更新变化
  void update();

  /* 各类型事件使用的 POLL 事件标志 */
  static const int kNoneEvent; // None 用于重置
  static const int kReadEvent; // 可读事件
  static const int kWriteEvent; // 可写事件

  EventLoop* loop_; // 每个 Channel 对象都属于某个线程的 EventLoop
  const int fd_; // 每个 Channel 负责一个 fd 的事件分发，注意该对象不拥有文件描述符
  int events_; // 监听的事件
  int revents_; // 一次事件循环中返回的事件
  int index_;  // 对象实例在其属于的 poller 的 pollfds_ 中的索引

  bool eventHandling_; // 当前是否正在处理事件
  /* 各事件类型的处理函数 */
  ReadEventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback errorCallback_;
  EventCallback closeCallback_;
};
}

#endif