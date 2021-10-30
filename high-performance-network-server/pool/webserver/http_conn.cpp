#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/uio.h>

#include "http_conn.h"
#include "../../epollutil.h"

/* 定义 HTTP 响应的一些状态信息 */
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The request file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
/* 网站的根目录 */
const char* doc_root = "/var/www/html";
/* 初始化静态成员 */
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/**
 * @brief 关闭客户连接，取消事件监听
 * 
 * @param real_close 
 */
void http_conn::close_conn(bool real_close) {
  if(real_close && m_sockfd != -1) {
    removefd(m_epollfd, m_sockfd); // 取消事件监听
    m_sockfd = -1;
    m_user_count--; // 关闭一个连接时，将客户总量减 1
  }
}

/**
 * @brief 初始化新接受的连接，外部使用的初始化接口
 * 
 * @param sockfd 客户连接套接字
 * @param address 客户连接地址 
 */
void http_conn::init(int sockfd, const sockaddr_in &address) {
  m_sockfd = sockfd;
  m_address = address;

  // 下面两行是为了避免 TIME_WAIT 状态，仅用于调试，实际使用时应该去掉
  int reuse = 1;
  setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  
  // 开始监听客户客户连接套接字
  addfd(m_epollfd, sockfd, EPOLLRDHUP | EPOLLONESHOT);
  m_user_count++;

  init();
}

/**
 * @brief 内部使用的初始化接口，初始化各数据，准备分析处理新的连接
 */
void http_conn::init() {
  m_check_state = CHECK_STATE_REQUSETLINE;
  m_linger = false;

  m_method = GET;
  m_url = nullptr;
  m_version = nullptr;
  m_content_length = 0;
  m_host = nullptr;
  m_start_line = 0;
  m_checked_idx = 0;
  m_read_idx = 0;
  m_write_idx = 0;

  memset(m_read_buf, '\0', READ_BUFFER_SIZE);
  memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
  memset(m_target_file_path, '\0', FILENAME_LEN);
}

/**
 * @brief 从状态机，确定读缓冲区中是否已经读取到完整的一行
 *
 * @return http_conn::LINE_STATUS
 */
http_conn::LINE_STATUS http_conn::parse_line() {
  char tmp;
  /**
   * 读缓冲区中 [0, m_checked_idx) 范围内的数据已经解析完毕，
   * 下面循环解析读缓冲区中 [m_checked_idx, m_read_idx) 范围内的数据
   * 如果读到连续的 "\r\n" 说明读到一个完整的行
   */
  for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
    tmp = m_read_buf[m_checked_idx]; // 当前需要分析的字节
    if (tmp == '\r') { 
      // 1. 当前字节是 \r ，说明可能读取到一个完整的行
      if ((m_checked_idx + 1) == m_read_idx) {
        /**
         * 1.1 当前字节 \r 是读缓冲区中的最后一个字节，则这次分析没有读到一个完整的行，
         *     返回 LINE_OPEN 表示还需要继续读取客户数据才能进一步分析
         */
        return LINE_OPEN;
      } else if (m_read_buf[m_checked_idx + 1] == '\n') {
        // 1.2 如果下一个字符是 \n ，则读到连续的 \r\n ，即成功读到一个完整的行
        m_read_buf[m_checked_idx++] = '\0';
        m_read_buf[m_checked_idx++] = '\0';
        return LINE_OK;
      } 
      // 语法错误，不能出现单独的 \r
      return LINE_BAD;
    } else if (tmp == '\n') {
      // 2. 当前字节是 \n ，说明可能读取到一个完整的行
      if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
        // 2.1 前一个字符是 \r ，则读到连续的 \r\n ，即成功读到一个完整的行
        // 会出现这种情况是因为 1.1 中恰好最后一个是 \r，则要在下一次分析中才能确定是否读到完整的行
        m_read_buf[m_checked_idx++] = '\0';
        m_read_buf[m_checked_idx++] = '\0';
        return LINE_OK;
      }
      // 语法错误，不能出现单独的 \n
      return LINE_BAD;
    }
  }
  /**
   * 如果解析完读缓冲区中的所有字节都没有遇到 \r，
   * 则返回 LINE_OPEN 表示还需要继续读取客户数据才能进一步分析
   */
  return LINE_OPEN;
}

/**
 * @brief 读取客户连接套接字上的数据到读缓冲区中
 * 
 * @return true 
 * @return false 
 */
bool http_conn::read() {
  if (m_read_idx >= READ_BUFFER_SIZE) // 缓冲区已满
    return false;

  int bytes_read = 0;
  // 循环读取客户连接数据，直到无数据可读或者对方关闭连接
  while (true) {
    // 将 socket 上的数据读取到读缓冲区中
    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                      READ_BUFFER_SIZE - m_read_idx, 0);
    if (bytes_read == -1) {
      if (errno == EAGAIN | errno == EWOULDBLOCK)  // 读完了，正常退出
        break;

      return false;                // 读取错误
    } else if (bytes_read == 0) {  // 对方关闭连接
      return false;
    }
    // 读取成功
    m_read_idx += bytes_read; // 更新读缓冲区大小标识
  }
  return true;
}

/**
 * @brief 解析 HTTP 请求行，获得请求方法、目标 URL、HTTP 版本号
 * 
 * @param text 
 * @return http_conn::HTTP_CODE 
 */
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
  m_url = strpbrk(text, " \t");
  if (!m_url) return BAD_REQUEST;
  *m_url++ = '\0';

  char* method = text;
  if (strcasecmp(method, "GET") != 0)  // TODO 当前只支持 GET 请求类型
    return BAD_REQUEST;
  m_method = GET;

  m_url += strspn(m_url, " \t");
  m_version = strpbrk(m_url, " \t");
  if (!m_version) return BAD_REQUEST;
  *m_version++ = '\0';
  m_version += strspn(m_version, " \t");
  if (strcasecmp(m_version, "HTTP/1.1") != 0) // TODO 当前只支持 HTTP1.1
    return BAD_REQUEST;

  if (strncasecmp(m_url, "http://", 7) == 0) {
    m_url += 7;
    m_url = strchr(m_url, '/');
  }

  if (!m_url || m_url[0] != '/') return BAD_REQUEST;

  // 处理完请求行，状态转移，下一步处理请求头
  m_check_state = CHECK_STATE_HEADER;
  return NO_REQUEST;
}

/**
 * @brief 解析 HTTP 请求的一个头部字段
 * 
 * @param text 
 * @return http_conn::HTTP_CODE 
 */
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
  if (text[0] == '\0') {  // 遇到空行，说明头部字段解析完毕
    // 如果 HTTP 请求有消息体，则还需读取 m_content_length 字节的消息体
    if (m_content_length != 0) {
      m_check_state = CHECK_STATE_CONTENT;
      return NO_REQUEST;
    }
    // 否则说明我们已经得到了一个完整的 HTTP 请求
    return GET_REQUEST;
  } else if (strncasecmp(text, "Contection:", 11) == 0) {
    text += 11;
    text += strspn(text, " \t");
    if (strcasecmp(text, "keep-alive") == 0) m_linger = true;
  } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
    text += 15;
    text += strspn(text, " \t");
    m_content_length = atol(text);
  } else if (strncasecmp(text, "Host:", 5) == 0) {
    text += 5;
    text += strspn(text, " \t");
    m_host = text;
  } else {
    printf("oop! unknow header %s\n", text);
    
  }

  return NO_REQUEST;
}

/**
 * @brief 解析 HTTP 请求体，
 *        TODO 这里并没有真正解析 HTTP 请求的消息体，只是判断他是否被完整读入了
 * 
 * @param text 
 * @return http_conn::HTTP_CODE 
 */
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
  if(m_read_idx >= (m_content_length + m_checked_idx)) {
    text[m_content_length] = '\0';
    return GET_REQUEST;
  }
  return NO_REQUEST;
}

/**
 * @brief 
 * 
 * @return char* 
 */
char* http_conn::get_line() {
  return m_read_buf + m_start_line;
}


/**
 * @brief 主状态机，根据当前状态解析读缓冲区中的数据
 * 
 * @return http_conn::HTTP_CODE 
 */
http_conn::HTTP_CODE http_conn::process_read() {
  LINE_STATUS line_status = LINE_OK;
  HTTP_CODE ret = NO_REQUEST;
  char* text = nullptr;

  while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
         ((line_status = parse_line()) == LINE_OK)) {
    text = get_line(); // 
    m_start_line = m_checked_idx; // 更新下一行的起始位置
    printf("got 1 http line: %s\n", text);

    switch (m_check_state) { // 判断主状态机当前状态
      case CHECK_STATE_REQUSETLINE:  // 解析请求行
        ret = parse_request_line(text);
        if (ret == BAD_REQUEST)  // 解析失败
          return BAD_REQUEST;
        break;
      case CHECK_STATE_HEADER:  // 解析请求头
        ret = parse_headers(text);
        if (ret == BAD_REQUEST)  // 解析失败
          return BAD_REQUEST;
        else if (ret == GET_REQUEST)  // 解析完成
          return do_request();
        break;
      case CHECK_STATE_CONTENT:  // 解析请求体
        ret = parse_content(text);
        if (ret == GET_REQUEST)  // 解析完成
          return do_request();

        line_status = LINE_OPEN;
        break;
      default:
        return INTERNAL_ERROR;
    }
  }

  return NO_REQUEST;
}

/**
 * @brief 当得到一个完整、正确的 HTTP 请求后，使用 mmap 将其映射到
 *        内存地址 m_file_address 处，并告诉调用者获取文件成功
 * 
 * @return http_conn::HTTP_CODE 
 */
http_conn::HTTP_CODE http_conn::do_request() {
  // 拼接目标文件的完整路径 = doc_root + m_url
  strcpy(m_target_file_path, doc_root);
  int len = strlen(doc_root);
  strncpy(m_target_file_path + len, m_url, FILENAME_LEN - len - 1);

  // 目标文件不存在
  if(stat(m_target_file_path, &m_file_stat) < 0)
    return NO_RESOURCE;
  // 目标文件不能被其他用户读取，即客户对资源没有足够的访问权限
  if(!(m_file_stat.st_mode & S_IROTH))
    return FORBINDDEN_RESOURCE;
  // 目标文件是个目录，不能返回目录
  if(S_ISDIR(m_file_stat.st_mode))
    return BAD_REQUEST;
  
  // 打开目标文件
  int fd = open(m_target_file_path, O_RDONLY);
  // 将目标文件映射到内存地址 m_file_address 处
  m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if(!m_file_address) // 映射失败
    return INTERNAL_ERROR;
  close(fd);
  return FILE_REQUEST;
}

/**
 * @brief 解除目标文件内存映射区 m_file_address 的映射
 */
void http_conn::unmap() {
  if(m_file_address) {
    munmap(m_file_address, m_file_stat.st_size);
    m_file_address = nullptr;
  }
}

/**
 * @brief 将写缓冲区中的 HTTP 响应字符串写入到客户连接套接字中
 * 
 * @return true 
 * @return false 
 */
bool http_conn::write() {
  int ret = 0;
  int bytes_have_send = 0; // 已经发送的字节数
  int bytes_to_send = m_write_idx; // 写缓冲区中需要发送的字节数
  if (bytes_to_send == 0) {
    modfd(m_epollfd, m_sockfd, EPOLLIN);
    init();
    return true;
  }

  while (true) {
    // writev 一次写多个非连续缓冲区
    // TODO 这里看起来没有处理实际写入大小比需求小的问题，https://stackoverflow.com/questions/45227781/how-to-deal-if-writev-only-write-part-of-the-data
    ret = writev(m_sockfd, m_iv, m_iv_count);
    if (ret < 0) {
      /**
       * 如果 TCP 写缓冲没有空间，则等下一轮 EPOLLOUT 事件，
       * 虽然在此期间，服务器无法立即接受到同一个客户的下一个请求，但这可保证连接的完整性
       */
      if (errno == EAGAIN) {
        // oneshot 模式，重新监听 可写事件
        modfd(m_epollfd, m_sockfd, EPOLLOUT);
        return true;
      }
      // 写入失败
      unmap();
      return false;
    }

    bytes_to_send -= ret;
    bytes_have_send += ret;
    // 发送 HTTP 响应成功
    if (bytes_to_send <= bytes_have_send) {
      unmap();
      if (m_linger) { // 保持连接
        init(); // 重新初始化以接受下一个请求
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return true;
      } else { // 关闭连接
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return false;
      }
    }
  }
}

/**
 * @brief 往写缓冲区中写入待发送的数据
 * 
 * @param format 
 * @param ... 可变形参，与 format 配合用于 printf 系列函数
 * @return 写入是否成功
 */
bool http_conn::add_response(const char* format, ...) {
  if (m_write_idx >= WRITE_BUFFER_SIZE) // 写缓冲区已满，直接退出
    return false;

  va_list arg_list;
  va_start(arg_list, format);
  // 往写缓冲区中写入格式化字符串
  int len = vsnprintf(m_write_buf + m_write_idx,
                      WRITE_BUFFER_SIZE - m_write_idx - 1, format, arg_list);
  if (len >= WRITE_BUFFER_SIZE - m_write_idx - 1) return false;

  m_write_idx += len; // 更新写缓冲区中待发送的字节数
  va_end(arg_list);
  return true;
}

bool http_conn::add_status_line(int status, const char * title) {
  return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
  return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
  return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger() {
  return add_response("Connection: %s\r\n", m_linger ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() {
  return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char * content) {
  return add_response("%s", content);
}

/**
 * @brief 根据服务器处理 HTTP 请求的结果 ret，决定返回给客户端的内容
 * 
 * @param ret 服务器处理 HTTP 请求的结果
 * @return true 
 * @return false 
 */
bool http_conn::process_write(HTTP_CODE ret) {
  switch (ret) {
    /* 各种错误 */
    case INTERNAL_ERROR:
      add_status_line(500, error_500_title);
      add_headers(strlen(error_500_form));
      if (!add_content(error_500_form)) return false;
      break;
    case BAD_REQUEST:
      add_status_line(400, error_400_title);
      add_headers(strlen(error_400_form));
      if (!add_content(error_400_form)) return false;
      break;
    case NO_RESOURCE:
      add_status_line(404, error_404_title);
      add_headers(strlen(error_404_form));
      if (!add_content(error_404_form)) return false;
      break;
    case FORBINDDEN_RESOURCE:
      add_status_line(403, error_403_title);
      add_headers(strlen(error_403_form));
      if (!add_content(error_403_form)) return false;
      break;
    /* 正确情况 */  
    case FILE_REQUEST:
      add_status_line(200, ok_200_title);
      if(m_file_stat.st_size != 0) {
        add_headers(m_file_stat.st_size);
        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        m_iv[1].iov_base = m_file_address;
        m_iv[1].iov_len = m_file_stat.st_size;
        m_iv_count = 2;
        return true;
      } else {
        const char* ok_string = "<html><body></body></html>";
        add_headers(strlen(ok_string));
        if(!add_content(ok_string))
          return false;
      }
      break;
    default:
      return false;
  }

  m_iv[0].iov_base = m_write_buf;
  m_iv[0].iov_len = m_write_idx;
  m_iv_count = 1;
  return true;
}

/**
 * @brief 由线程池中的工作线程调用，这是处理 HTTP 请求的入口函数
 */
void http_conn::process() {
  // 解析读缓冲区中的数据
  HTTP_CODE read_ret = process_read();
  // 解析结果是请求还未完整接收，继续监听 socket 可读事件
  if(read_ret == NO_REQUEST) {
    // 因为设置的是 oneshot 事件，所以这边要重置 EPOLLIN
    modfd(m_epollfd, m_sockfd, EPOLLIN);
    return;
  }
  // 处理 HTTP 请求成功或失败，向写缓冲区中填充响应数据
  bool write_ret = process_write(read_ret);
  if(!write_ret)
    close_conn();
  else // 填充成功，开始监听 socket 可写事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}