#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>  // 包含相关头文件
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

// 连接资源 结构体成员 要用到 定时器类
// 前向声明
class util_timer;
 
// 连接资源
struct client_data
{
    // 客户端 socket 地址
    sockaddr_in address;
 
    // socket 文件描述符
    int sockfd;
 
    // 定时器
    util_timer* timer; // 指向 util_timer
};
 
// 定时器类
class util_timer
{
public:
    // 构造函数          成员初始化列表
    util_timer() : prev(NULL), next(NULL) {}
 
public:
    // 超时时间
    time_t expire;
    // 回调函数
    void (*cb_func)(client_data*); // 函数指针
    // 连接资源
    client_data* user_data; // 指向 client_data
    // 前向定时器
    util_timer* prev;
    // 后继定时器
    util_timer* next;
};

class sort_timer_lst
{
public:
    sort_timer_lst();  // 构造函数
    ~sort_timer_lst();  // 析构函数

    void add_timer(util_timer *timer);  // 添加定时器
    void adjust_timer(util_timer *timer);  // 调整定时器
    void del_timer(util_timer *timer);  // 删除定时器
    void tick();  // 检查定时器是否到期

private:
    // 辅助函数，将定时器插入链表
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;  // 链表头指针
    util_timer *tail;  // 链表尾指针
};

class Utils
{
public:
    Utils() {}  // 构造函数
    ~Utils() {}  // 析构函数

    void init(int timeslot);  // 初始化函数

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);  // 设置文件描述符为非阻塞模式

    // 内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    // 将文件描述符添加到epoll内核事件表中
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);  // 信号处理函数

    // 设置信号函数
    // 注册信号处理函数
    void addsig(int sig, void(handler)(int), bool restart = true);  

    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();  // 定时器处理函数

    void show_error(int connfd, const char *info);  // 显示错误信息

public:
    static int *u_pipefd;  // 静态管道文件描述符
    sort_timer_lst m_timer_lst;  // 定时器链表
    static int u_epollfd;  // 静态epoll文件描述符
    int m_TIMESLOT;  // 时间槽
};

void cb_func(client_data *user_data);  // 回调函数声明

#endif  // 结束标记
