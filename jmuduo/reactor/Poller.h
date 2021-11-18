#ifndef _JMUDUO_POLLER_H_
#define _JMUDUO_POLLER_H_

#include <unordered_map>
#include <vector>

#include "EventLoop.h"
#include "noncopyable.h"

struct pollfd;

namespace jmuduo {

class Channel;

/**
 * @brief 封装 IO 复用对象
 * TODO 这应该是个抽象基类，以支持多种 IO 复用方式
 */
class Poller : noncopyable {
 public:
  using ChannelList = std::vector<Channel*>;

  Poller(EventLoop*);
  // Poller 不拥有 Channels，
  ~Poller();

  /**
   * @brief 进入 IO 复用等待，必须在事件循环线程调用
   * 
   * @param timeoutMs 最长等待时间
   * @param activeChannels 事件循环返回时有事件发生的信道
   * @return int 返回的时间
   */
  int poll(int timeoutMs, ChannelList* activeChannels);

  // 改变一个信道关心的事件，必须在事件循环线程调用
  void updateChannel(Channel*);
  // 包装线程判断
  void assertInLoopThread() { ownerLoop_->assertInLoopThread(); };

 private:
  void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;

  using PollFdList = std::vector<struct pollfd>;
  using ChannelMap = std::unordered_map<int, Channel*>;

  EventLoop* ownerLoop_;  // 拥有这个IO复用对象的事件循环对象
  PollFdList pollfds_; // 监听的所有 fd
  ChannelMap channels_; // fd => channel*，由 fd 寻找对应的信道，用在填充活动信道函数中
};

}  // namespace jmuduo

#endif