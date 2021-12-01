#ifndef _JMUDUO_SOCKETSOPS_H_
#define _JMUDUO_SOCKETSOPS_H_

#include <arpa/inet.h>
#include <endian.h>

namespace jmuduo {

namespace sockets { // 包装 os 提供的 socket 基本操作

/* 本地字节序 与 网络字节序 的相互转换 */

inline uint64_t hostToNetwork64(uint64_t host64) { return htobe64(host64); }

inline uint32_t hostToNetwork32(uint32_t host32) { return htonl(host32); }

inline uint16_t hostToNetwork16(uint16_t host16) { return htons(host16); }

inline uint64_t networkToHost64(uint64_t net64) { return be64toh(net64); }

inline uint32_t networkToHost32(uint32_t net32) { return ntohl(net32); }

inline uint16_t networkToHost16(uint16_t net16) { return ntohs(net16); }

/* socket 基本操作 */

// 创建一个非阻塞的 socket fd，失败时直接 abort
int createNonblockingOrDie();
// 绑定 sockfd 与本地地址 addr
void bindOrDie(int sockfd, const struct sockaddr_in& addr);
// 开始监听
void listenOrDie(int sockfd);
// 接受 sockfd 上的连接，返回一个代表新连接的 socket，新 socket 的远端地址为 addr
int accept(int sockfd, struct sockaddr_in* addr);
void close(int sockfd);

/* 地址转换 */

// 解析远端地址 addr（网络字节序），以 "ip:port" 的形式存储在 buf 中
void toHostPort(char* buf, size_t bufSize, const struct sockaddr_in& addr);
// 将 ip port 拼接成将要使用的远端地址（网络字节序）
void fromHostPort(const char* ip, uint16_t port, struct sockaddr_in* addr);
}  // namespace sockets

}  // namespace jmuduo

#endif