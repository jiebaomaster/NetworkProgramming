#define _GNU_SOURCE 1
#include "Channel.h"
#include "EventLoop.h"
#include "../base/logging/Logging.h"

#include <sys/poll.h>
#include <assert.h>

using namespace jmuduo;

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kWriteEvent = POLLOUT;

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop),
      fd_(fd),
      events_(0),
      revents_(0),
      index_(-1),
      eventHandling_(false) {}

Channel::~Channel() {
  // 对象正在处理回调函数时，不能析构
  assert(!eventHandling_);
}

void Channel::update() {
  loop_->updateChannel(this);
}

void Channel::handleEvent() {
  eventHandling_ = true;
  if (revents_ & POLLNVAL) {
    LOG_WARN << "Channel::handle_event() POLLNVAL";
  }

  if ((revents_ & POLLHUP) && !(revents_ & POLLIN)) {
    LOG_WARN << "Channel::handle_event() POLLHUP";
    if (closeCallback_) closeCallback_();
  }

  // 有错误事件，调用错误处理
  if (revents_ & (POLLNVAL | POLLERR))
    if (errorCallback_) errorCallback_();

  // 有可读事件，调用可读处理
  if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))
    if (readCallback_) readCallback_();

  // 有可写事件，调用可写处理
  if (revents_ & POLLOUT)
    if (writeCallback_) writeCallback_();

  eventHandling_ = false;
}
