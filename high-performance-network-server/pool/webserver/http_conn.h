#ifndef _HTTP_CONN_H_
#define _HTTP_CONN_H_

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../locker.h"

/**
 * 
 * 
 */
class http_conn {
 public:
  static const int FILENAME_LEN = 200;        // 文件名长度
  static const int READ_BUFFER_SIZE = 2048;   // 读缓冲区大小
  static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区大小
  enum METHOD {  // HTTP 请求方法，TODO 目前仅支持 GET
    GET = 0,
    POST,
    HEAD,
    PUT,
    DELETE,
    TRACE,
    OPTIONS,
    CONNECT,
    PATCH
  };
  enum CHECK_STATE {  // 解析客户请求时，主状态机所处的状态
    CHECK_STATE_REQUSETLINE = 0,  // 当前正在分析请求行
    CHECK_STATE_HEADER, // 当前正在分析头部字段
    CHECK_STATE_CONTENT // 当前正在分析请求体
  };
  enum HTTP_CODE {  // 服务器处理 HTTP 请求可能的结果
    NO_REQUEST, // 请求不完整，需要继续读取客户数据
    GET_REQUEST, // 获得了一个完整的客户请求
    BAD_REQUEST, // 客户请求有语法错误
    NO_RESOURCE, // 客户请求的资源不存在
    FORBINDDEN_RESOURCE, // 客户对资源没有足够的访问权限
    FILE_REQUEST, // 解析成功，且找到目标文件
    INTERNAL_ERROR, // 服务器内部错误
    CLOSED_CONNECTION // 客户端已经关闭连接了
  };
  enum LINE_STATUS {  // 行的读取状态
    LINE_OK = 0, // 读取到一个完整的行
    LINE_BAD, // 行出错
    LINE_OPEN // 行数据尚且不完整
  };

 public:
  http_conn() = default;
  ~http_conn() = default;

 public: // 供外部使用者调用的接口
  // 初始化新接受的连接
  void init(int sockfd, const sockaddr_in& addr);
  // 关闭连接
  void close_conn(bool real_close = true);
  // 处理客户请求
  void process();
  // 非阻塞读操作
  bool read();
  // 非阻塞写操作
  bool write();

 private:
  // 初始化连接
  void init();
  // 解析 http 请求
  HTTP_CODE process_read();
  // 填充 http 应答
  bool process_write(HTTP_CODE ret);

  /**
   * HTTP 请求格式
   *                                            分析函数
   * 请求行   GET http://xxx/xxx.html HTTP/1.0   parse_request_line
   * 头部字段 Contection: keep-alive             parse_headers
   * 头部字段 Host: www.baidu.com
   * 头部字段 Content-Length: xxx
   * 空行分割
   * 消息体   xxxx                               parse_content
   */
  /* 下面这一组函数被 process_read 调用以分析 http 请求 */
  HTTP_CODE parse_request_line(char* text);
  HTTP_CODE parse_headers(char* text);
  HTTP_CODE parse_content(char* text);
  HTTP_CODE do_request();
  char* get_line();
  LINE_STATUS parse_line();

  /**
   * HTTP 响应格式
   *                                   填充函数
   * 状态行   HTTP/1.1 200 OK           add_status_line
   * 头部字段 Content-Length: xxx       add_content_length
   * 头部字段 Connection: keep-alive    add_linger
   * 空行分割                           add_blank_line
   * 应答内容 ...response content...    add_content
   */
  /* 下面这一组函数被 process_write 调用以填充 http 应答 */
  void unmap();
  /* add_xxx 按 http 协议填充写缓冲区 */
  bool add_response(const char* fmt, ...);
  bool add_content(const char* content);
  bool add_status_line(int status, const char* title);
  bool add_headers(int content_length);
  bool add_content_length(int content_length);
  bool add_linger();
  bool add_blank_line();

 public:
  /* 一个 http_conn 的实例代表一个 http 连接，下面两个静态成员是所有连接的共享信息 */
  // reactor 模式中，所有的 socket 都被同一个 epoll 监听，所以该变量为静态的
  static int m_epollfd;  
  static int m_user_count;  // 统计当前用户数量

 private:
  // 该 http 连接的 socket 和对方的 socket 地址
  int m_sockfd;
  sockaddr_in m_address;

  char m_read_buf[READ_BUFFER_SIZE];  // 读缓冲区
  int m_read_idx;  // 标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
  int m_checked_idx;  // 当前正在分析的字符在读缓冲区中的位置
  int m_start_line;   // 当前正在解析的行的起始位置
  char m_write_buf[WRITE_BUFFER_SIZE];  // 写缓冲区
  int m_write_idx;                      // 写缓冲区中待发送的字节数

  CHECK_STATE m_check_state;  // 主状态机当前所处的状态
  METHOD m_method;            // 当前请求的方法

  /* 解析 http 请求得到的信息 */
  // 客户请求的目标文件的完整路径，其内容 = 网站根目录 + m_url
  char m_target_file_path[FILENAME_LEN];
  char* m_url;           // 客户请求的目标文件的文件名
  char* m_version;       // http 协议版本号，我们仅支持 HTTP/1.1
  char* m_host;          // 主机名
  int m_content_length;  // http 请求消息体的长度
  bool m_linger;         // http 请求是否要求保持连接

  char* m_file_address;  // 客户请求的目标文件被 mmap 到内存中的起始位置
  struct stat
      m_file_stat;  // 目标文件的状态。判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
  // 采用 writev 来执行写操作，定义下面两个辅助变量
  struct iovec m_iv[2];
  int m_iv_count;  // 表示被写内存块的数量
};

#endif