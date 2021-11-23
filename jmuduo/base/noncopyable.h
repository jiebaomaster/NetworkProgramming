#ifndef _JMUDUO_NONCOPYABLE_H_
#define _JMUDUO_NONCOPYABLE_H_

namespace jmuduo {

/**
 * @brief 基类，继承他的子类不能拷贝
 */
class noncopyable {
 public:
  // 不能进行拷贝构造
  noncopyable(const noncopyable&) = delete;
  // 不能进行拷贝赋值
  void operator=(const noncopyable&) = delete;

 protected:
  noncopyable() = default;
  ~noncopyable() = default;
};

/**
 * @brief 空基类，强调其子类是可复制的
 * 任何可复制的派生类都应该是值类型
 */
class copyable {};

}  // namespace muduo

#endif