/* $Id: socket.c 1.1 1995/01/01 07:11:14 cthuang Exp $
 *
 * This module has been modified by Radim Kolar for OS/2 emx
 */

/***********************************************************************
  module:       socket.c
  program:      popclient
  SCCS ID:      @(#)socket.c    1.5  4/1/94
  programmer:   Virginia Tech Computing Center
  compiler:     DEC RISC C compiler (Ultrix 4.1)
  environment:  DEC Ultrix 4.3 
  description:  UNIX sockets code.
 ***********************************************************************/

#include <sys/types.h>
#include <sys/socket.h> // hostent
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> // hostent
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

// 防止报错 结构 "hostent" 没有字段 "h_addr"
#define h_addr h_addr_list[0]

int Socket(const char *host, int clientPort)
{
    int sock;
    unsigned long inaddr;
    struct sockaddr_in ad;
    struct hostent *hp;

    memset(&ad, 0, sizeof(ad)); /* 初始化ad结构体为0 */
    ad.sin_family = AF_INET; /* 设置地址族为IPv4 */

    inaddr = inet_addr(host); /* 将IP地址转换为网络字节序的二进制数 */
    if (inaddr != INADDR_NONE)
        memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
    else
    {
        hp = gethostbyname(host); /* 获取主机信息 */
        if (hp == NULL)
            return -1;
        memcpy(&ad.sin_addr, hp->h_addr, hp->h_length); /* 复制主机地址信息 */
    }
    ad.sin_port = htons(clientPort); /* 设置端口号为网络字节序 */

    sock = socket(AF_INET, SOCK_STREAM, 0); /* 创建套接字 */
    if (sock < 0)
        return sock;
    if (connect(sock, (struct sockaddr *)&ad, sizeof(ad)) < 0) /* 连接服务器 */
        return -1;
    return sock;
}
