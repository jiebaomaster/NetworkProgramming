#include "Poller.h"
#include "Channel.h"
#include "../base/logging/Logging.h"

#include <assert.h>
#include <sys/poll.h>
#include <assert.h>

#include <algorithm>

using namespace jmuduo;

Poller::Poller(EventLoop* ownerLooper) : ownerLoop_(ownerLooper) {}

Poller::~Poller() {}

Timestamp Poller::poll(int timeoutMs, ChannelList* activeChannels) {
  int numEvents = ::poll(&(pollfds_[0]), pollfds_.size(), timeoutMs);
  Timestamp now(Timestamp::now());
  if (numEvents > 0) {  // 如果有事件发生
    LOG_TRACE << numEvents << " events happened";
    // 收集所有有事件处理的信道
    fillActiveChannels(numEvents, activeChannels);
  } else if (numEvents == 0) {
    LOG_TRACE << "nothing happned";
  } else {
    LOG_SYSERR << "ERR Poller::poll()";
  }

  return now;
}

/**
 * @brief 遍历监听列表，收集所有有待处理事件（active）的信道
 * 事件处理分为两步:
 * 1. 在本函数中搜集所有有事件触发的信道，通过 activeChannels 传给 EventLoop
 * 2. 在 EventLoop.loop() 中进行所有信道的处理 channel.handleEvent()
 * 不能直接在 poller.poll() 中进行信号处理，因为信号处理有可能会在遍历期间
 * 修改 poller.pollfds_，这是非常危险的。另一方面也是为了简化 Poller 
 * 的职责，它只负责 IO 复用，不负责事件分发，这样支持多种 IO 复用方式时更方便
 * 
 * @param numEvents 总共有事件触发的 pollfd 个数
 * @param activeChannels
 */
void Poller::fillActiveChannels(int numEvents,
                                ChannelList* activeChannels) const {
  for (auto itfd = pollfds_.begin(); itfd != pollfds_.end() && numEvents > 0;
       itfd++) {
    if (itfd->revents > 0) {  // 当前监听的 fd 上有事件发生
      --numEvents;
      auto ch = channels_.find(itfd->fd);
      assert(ch != channels_.end());
      Channel* pchannel = ch->second;
      pchannel->set_revents(itfd->revents);  // 填充发生的事件类型
      activeChannels->push_back(pchannel);
    }
  }
}

void Poller::updateChannel(Channel* channel) {
  assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
  if (channel->index() < 0) {
    /* 一个新的信道，新建一个 pollfd 并添加到 pollfds_ 中*/
    assert(channels_.find(channel->fd()) == channels_.end());
    struct pollfd pollfd; // 分配一个新 pollfd
    pollfd.fd = channel->fd();
    pollfd.events = static_cast<short>(channel->events());
    pollfd.revents = 0;
    pollfds_.push_back(pollfd);
    channel->set_index(static_cast<int>(pollfds_.size()) - 1);
    channels_[pollfd.fd] = channel;
  } else {
    /* 更新一个已存在的信道的 pollfd */
    assert(channels_.find(channel->fd()) != channels_.end());
    assert(channels_[channel->fd()] == channel);
    int idx = channel->index();
    assert(idx >= 0 && idx < static_cast<int>(channels_.size()));
    struct pollfd& pollfd = pollfds_[idx];
    assert(pollfd.fd == channel->fd() || pollfd.fd == -channel->fd()-1);
    pollfd.events = static_cast<short>(channel->events());
    pollfd.revents = 0;
    // 若该 channel 此时不监听任何事件，将 fd 设成负数表示忽略这个 pollfd
    if(channel->isNoneEvent())
      pollfd.fd = -channel->fd()-1;
  }
}

void Poller::removeChannel(Channel* channel) {
  assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd();
  assert(channels_.find(channel->fd()) != channels_.end());
  assert(channels_[channel->fd()] == channel);
  assert(channel->isNoneEvent());
  int idx = channel->index();
  assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
  const struct pollfd& pfd = pollfds_[idx]; // 待删除的 pollfd
  assert(pfd.fd == -channel->fd()-1 && pfd.events == channel->events());
  // 从 map 中移除
  size_t n = channels_.erase(channel->fd());
  assert(n == 1);
  // 从 pollfds_ 中移除
  if (static_cast<size_t>(idx) == pollfds_.size() - 1) {
    // 最后一个，可以直接删除
    pollfds_.pop_back();
  } else { // 要删除的不是最后一个，先和最后一个交换，再删除最后一个
    int channelAtEnd = pollfds_.back().fd;
    std::iter_swap(pollfds_.begin() + idx, pollfds_.end() - 1);
    if (channelAtEnd < 0) {
      channelAtEnd = -channelAtEnd - 1;
    }
    channels_[channelAtEnd]->set_index(idx); // 更新被交换信道的 index
    pollfds_.pop_back(); // 删除当前最后一个
  }
}