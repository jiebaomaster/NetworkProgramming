#include <arpa/inet.h>
#include <assert.h>
#include <libgen.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("usage: %s ip_address port_number\n", basename(argv[0]));
    return 1;
  }

  // 解析命令行参数
  const char* ip = argv[1];
  int port = atoi(argv[2]);

  // 创建一个 IPV4 地址
  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;               // protocol ipv4
  inet_pton(AF_INET, ip, &address.sin_addr);  // ip
  address.sin_port = htons(port);             // port

  int sockfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(sockfd >= 0);

  // 连接到服务器
  if (connect(sockfd, (struct sockaddr*)&address, sizeof(address)) < 0) {
    printf("connection failed!\n");
  } else {
    const char* oob_data = "adc";
    const char* normal_data = "123";

    // 发送数据
    int ret = 0;
    ret = send(sockfd, normal_data, strlen(normal_data), 0);
    assert(ret >= 0);
    ret = send(sockfd, oob_data, strlen(oob_data), MSG_OOB);
    assert(ret >= 0);
    ret = send(sockfd, normal_data, strlen(normal_data), 0);
    assert(ret >= 0);
  }

  close(sockfd);
  return 0;
}