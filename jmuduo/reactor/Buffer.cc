#include "Buffer.h"

#include <sys/uio.h>

using namespace jmuduo;


/**
 * P208 P315
 * 缓冲区的大小设置是一个问题。一方面我们希望 减少系统调用，缓冲区应该够大，因为每次读的数据
 * 越多越划算；另一方面希望 减少内存占用，因为大多数时候这些缓冲区的使用率很低。
 * muduo 用 readv 结合栈上空间巧妙地解决了这个问题。 
 * 1. 使用 scatter/gather I/O，额外的缓冲区取自 stack。利用了临时的栈上空间，避免每个连接
 *    的初始 Buffer 过大造成内存浪费。额外的缓冲区也使得输入缓冲区足够大，通常一次 readv 
 *    就能取完全部数据，节省系统调用。
 * 2. Buffer::readFd 只调用一次 read，没有反复调用 read 直到返回 EAGAIN。
 *    首先，因为 muduo 采用 level trigger，这么做不会丢失数据。其次，对追求低延迟的程序来说，
 *    这么做是高效的，因为每次读数据只需一次 read，而 edge trigger 每次最少两次 read。
 *    再次，这样做照顾了多个连接的公平性，不会因为每个连接上数据量过大而影响其他连接处理消息。
 */
ssize_t Buffer::readFd(int fd, int* savedErrno) {
  // 在栈上开一个临时缓冲区，确保缓冲区足够大，一次 readv 能够读完所有数据
  char extrabuf[65536];
  struct iovec vec[2];  // scatter/gather I/O
  const size_t writable = writableBytes();
  // 先使用 buffer_
  vec[0].iov_base = begin() + writerIndex_;
  vec[0].iov_len = writable;
  // 写满 buffer_ 后再使用 extrabuf
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof extrabuf;
  ssize_t n = readv(fd, vec, 2); 
  if (n < 0) { // 读取失败
    *savedErrno = errno;
  } else if (static_cast<size_t>(n) <= writable) { // 只使用了 buffer_
    writerIndex_ += n;
  } else { // buffer_ 写满了，使用了 extrabuf
    writerIndex_ += buffer_.size();
    append(extrabuf, n - writable); // 将 extrabuf 中的数据添加到缓冲区
  }
  // TODO 缓冲区还是有可能不够大，如果 n==writable+sizeof extrabuf，就再读一次

  return n;
}
