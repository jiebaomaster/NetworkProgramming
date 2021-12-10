#ifndef _JMUDUO_BUFFER_H_
#define _JMUDUO_BUFFER_H_

#include <assert.h>

#include <string>
#include <vector>

#include "noncopyable.h"

namespace jmuduo {

/**
 * 可变长度的缓冲区，用于非阻塞 IO 的读写缓冲
 * 可读区域的前部总是保留了一小段可写区域，方便在可读区域的前方写入，
 * 所以缓冲区的总大小为 prependableBytes + readableBytes + writableBytes
 *
 * A buffer class modeled after org.jboss.netty.buffer.ChannelBuffer
 *        前部可写区域          可读区域            可写区域
 *  +-------------------+------------------+------------------+
 *  | prependable bytes |  readable bytes  |  writable bytes  |
 *  |                   |     (CONTENT)    |                  |
 *  +-------------------+------------------+------------------+
 *  |                   |                  |                  |
 *  0      <=      readerIndex   <=   writerIndex    <=     size
 */
class Buffer : copyable {
 public:
  static const size_t kCheapPrepend = 8;    // 前部可写区域的默认大小（最小大小）
  static const size_t kInitialSize = 1024;  // 可写区域的初始大小

  Buffer()
      : buffer_(kCheapPrepend + kInitialSize),
        readerIndex_(kCheapPrepend),
        writerIndex_(kCheapPrepend) {
    assert(readableBytes() == 0);
    assert(writableBytes() == kInitialSize);
    assert(prependableBytes() == kCheapPrepend);
  }

  void swap(Buffer& rhs) {
    buffer_.swap(rhs.buffer_);
    std::swap(readerIndex_, rhs.readerIndex_);
    std::swap(writerIndex_, writerIndex_);
  }

  size_t readableBytes() const { return writerIndex_ - readerIndex_; }

  size_t writableBytes() const { return buffer_.size() - writerIndex_; }

  size_t prependableBytes() const { return readerIndex_; }

  /* 读取操作 */

  // 返回可读区域的起始地址
  const char* peek() const { return begin() + readerIndex_; }

  // 读取 len 个字节，可读索引后移 len。该函数返回 void，防止像下面这样被使用
  // string str(retrieve(readableBytes()), readableBytes());
  // 表达式中两个函数的执行顺序是不确定的
  void retrieve(size_t len) {
    assert(len < readableBytes());
    readerIndex_ += len;
  }

  // 读取 [peek(), end) 范围内的内容
  void retrieveUntil(const char* end) {
    assert(peek() <= end);
    assert(end <= beginWrite());
    retrieve(end - peek());
  }

  // 读取所有字符，两个索引复位
  void retrieveAll() {
    readerIndex_ = kCheapPrepend;
    writerIndex_ = kCheapPrepend;
  }

  // 读取所有字符，并保存在 string 中返回
  std::string retrieveAsString() {
    std::string str(peek(), readableBytes());
    retrieveAll();
    return str;
  }

  /* 写入操作 */

  // 写入一个字符串
  void append(const std::string& str) { append(str.c_str(), str.length()); }

  // 写入 [data, data+len)
  void append(const char* /*restrict*/ data, size_t len) {
    ensureWritableBytes(len);
    std::copy(data, data + len, beginWrite());
    hasWritten(len);
  }

  void append(const void* /*restrict*/ data, size_t len) {
    append(static_cast<const char*>(data), len);
  }
  // 确保可写区域足够大
  void ensureWritableBytes(size_t len) {
    if (writableBytes() < len) {  // 可写区域太小，需要扩容
      makeSpace(len);
    }
    assert(writableBytes() >= len);
  }

  // 返回可写区域的起始地址
  char* beginWrite() { return begin() + writerIndex_; }
  const char* beginWrite() const { return begin() + writerIndex_; }
  // 更新可写索引
  void hasWritten(size_t len) { writerIndex_ += len; }

  // 向前部可写区域写入
  void prepend(const void* /**/ data, size_t len) {
    assert(len <= prependableBytes());
    readerIndex_ -= len;
    const char* d = static_cast<const char*>(data);
    std::copy(d, d + len, begin() + readerIndex_);
  }
  // 手动收缩缓冲区的大小，为了避免频繁分配内存，vector 缓冲区只会自动扩容，不会自动收缩
  // 收缩后的缓冲区大小为 prependableBytes + readableBytes + reserve
  void shrink(size_t reserve) {
    std::vector<char> buf(kCheapPrepend + readableBytes() + reserve);
    std::copy(peek(), peek() + readableBytes(), buf.begin() + kCheapPrepend);
    buf.swap(buf);
  }

  /**
   * @brief 读取 fd 上的数据到缓冲区中
   * @return 成功时返回读取的字节数，失败时返回负数，并在 savedErrno 中保存错误原因
   */
  ssize_t readFd(int fd, int* savedErrno);


 private:
  // 返回缓冲区的起始地址
  char* begin() { return &*buffer_.begin(); }
  const char* begin() const { return &*buffer_.begin(); }

  // 可写区域不够大，扩容或者移动内容，确保可写区域的大小不小于 len
  void makeSpace(size_t len) {
    /**
     * P215 
     * 读取操作会使得前部可写区域的范围变大，如果“前部可写区域和后部可写区域”加起来足够
     * 容纳 “待写入大小 len 和前部可写区域最小大小”，那么可以将已有数据向前移动到
     * begin()+kCheapPrepend 开始的区域，来腾出足够的空间。
     * 移动将导致内存拷贝，但是扩容缓冲区时，也是要拷贝数据到新分配的内存区域的
     */
    if(writableBytes() + prependableBytes() < len + kCheapPrepend) {
      // 不够大，需要扩容
      // TODO vector.resize 扩容出来的空间会被初始化为 0，这种初始化是多余的
      buffer_.resize(writerIndex_ + len);
    } else { // 足够大，将已有数据向前移动，腾出空间
      assert(kCheapPrepend < readerIndex_);
      size_t readable = readableBytes();  // 保存已有数据长度
      // 向前移动，不存在覆盖问题
      std::copy(begin() + readerIndex_, begin() + writerIndex_,
                begin() + kCheapPrepend);
      readerIndex_ = kCheapPrepend;
      writerIndex_ = readerIndex_ + readable;
      assert(readable == readableBytes());
    }
  }

  std::vector<char> buffer_;  // 缓冲区
  // 因为 Buffer 使用了 vector 的可变长度数组作为缓冲区，在 vector 扩容的时候会
  // 导致指针失效，所以这里使用索引保存缓冲区内各区域的位置
  size_t readerIndex_;  // 缓冲区的可读索引
  size_t writerIndex_;  // 缓冲区的可写索引
};

}  // namespace jmuduo

#endif