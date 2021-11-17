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

}  // namespace muduo

#endif