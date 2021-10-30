#ifndef LIST_TIMER_H
#define LIST_TIMER_H

#include <netinet/in.h>
#include <time.h>

#include <iostream>

#define BUFF_SIZE 64
struct util_timer;

// 客户数据
struct client_data {
  sockaddr_in address;  // 客户端地址
  int sockfd;           // socket
  char buf[BUFF_SIZE];  // 读缓存
  util_timer *timer;    // 定时器
};

struct util_timer {
  util_timer() : prev(nullptr), next(nullptr) {}

  using timer_handler = void(client_data *);  // 定义定时器回调函数类型

  time_t expire;          // 任务超时时间，这里使用绝对时间
  timer_handler *cb_func;  // 定时器回调函数
  client_data
      *user_data;  // 回调函数处理的客户数据，由定时器的执行者传递给回调函数

  util_timer *prev;
  util_timer *next;
};

class sort_timer_list {
 public:
  sort_timer_list() = default;
  // 列表销毁时，删除其中所有定时器
  ~sort_timer_list() {
    util_timer *tmp = head;
    while (tmp) {
      head = tmp->next;
      delete tmp;
      tmp = head;
    }
  }
  /**
   * @brief 添加一个定时器到链表中
   *
   * @param timer
   */
  void add_timer(util_timer *timer) {
    if (!timer) return;
    if (!head) {
      head = tail = timer;
      return;
    }
    if (timer->expire < head->expire) {
      timer->next = head;
      head->prev = timer;
      head = timer;
      return;
    }
    add_timer(timer, head);
  }
  /**
   * @brief 若某个定时任务发生变化时，调整该定时器在链表中的位置。
   * 只考虑被调整的定时器的超时时间延长的情况，即需要往链表尾部移动
   *
   * @param timer
   */
  void adjust_timer(util_timer *timer) {
    if (!timer) return;
    // 尾节点，或者调整后超时时间仍小于后一个节点，则不用移动
    if (timer == tail || timer->expire < timer->next->expire) return;

    if (timer == head) {  // 头节点
      head = timer->next;
      head->prev = nullptr;
      timer->next = nullptr;
      add_timer(timer, head);
    } else {  // 中间结点
      timer->next->prev = timer->prev;
      timer->prev->next = timer->next;
      add_timer(timer, timer->next);
    }
  }
  /**
   * @brief 删除一个定时器
   *
   * @param timer
   */
  void del_timer(util_timer *timer) {
    if (!timer) return;
    // 从链表中移除定时器节点
    if (timer == head && timer == tail) {  // 链表中只有一个节点
      head = tail = nullptr;
    } else if (timer == head) {  // 头节点
      head = timer->next;
      head->prev = nullptr;
    } else if (timer == tail) {  // 尾节点
      tail = timer->prev;
      tail->next = nullptr;
    } else {
      timer->next->prev = timer->prev;
      timer->prev->next = timer->next;
    }
    delete timer;
  }

  /**
   * @brief SIGALRM 信号每次被触发就在其信号处理函数（如果统一事件源，则是主函数）
   * 中执行一次 tick 函数，以处理链表上到期的任务
   */
  void tick() {
    if (!head) return;

    std::cout << "timer tick" << std::endl;
    time_t cur = time(NULL);
    util_timer *timer = head;
    util_timer *tmp = nullptr;
    // 从头到尾依次处理每个定时器，直到遇到一个尚未到期的定时器
    while (timer) {
      if (cur < timer->expire) break;

      timer->cb_func(timer->user_data);
      tmp = timer->next;
      del_timer(timer);
      timer = tmp;
    }
  }

 private:
  /**
   * @brief 辅助函数，将节点添加到 list_head 之后的链表中
   *
   * @param timer
   * @param list_head
   */
  void add_timer(util_timer *timer, util_timer *list_head) {
    util_timer *tmp = list_head->next;
    while (tmp) {                         // 找到第一个大于的节点
      if (tmp->expire > timer->expire) {  // 插入
        timer->prev = tmp->prev;
        timer->next = tmp;
        tmp->prev->next = timer;
        tmp->prev = timer;
        break;
      }
      tmp = tmp->next;
    }
    // 遍历整个列表没找到插入位置，则插在最后
    if (tmp == nullptr) {
      tail->next = timer;
      timer->prev = tail;
      tail = timer;
      tail->next = nullptr;
    }
  }

  util_timer *head;
  util_timer *tail;
};

#endif