#include "lst_timer.h"  
#include "../http/http_conn.h"  

sort_timer_lst::sort_timer_lst()  // 定时器链表构造函数
{
    head = NULL; 
    tail = NULL;  
}

sort_timer_lst::~sort_timer_lst()  // 定时器链表析构函数
{
    util_timer *tmp = head;  // 临时指针指向头指针
    while (tmp) // 删除链表所有定时器
    {
        head = tmp->next;  // 头指针后移
        delete tmp;  // 释放内存
        tmp = head;  // 更新临时指针
    }
}

void sort_timer_lst::add_timer(util_timer *timer)  // 添加定时器
{
    if (!timer)  // 定时器为空
    {
        return;
    }
    if (!head)  // 链表为空则直接插入
    {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire)  // 若定时器到期时间小于头结点，则插入头结点之前
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);  // 调用辅助函数在链表中插入定时器
}

void sort_timer_lst::adjust_timer(util_timer *timer)  // 调整链表中的定时器
{
    if (!timer) return;
        util_timer* tmp = timer->next;
 
    // 被调整的定时器在 链表尾部
    // 定时器超时值，仍小于下一个定时器超时值，不调整
    if (!tmp || (timer->expire < tmp->expire))
        return;
 
    // 被调整定时器，是链表头节点，将定时器取出，重新插入
    if (timer == head) {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
 
    // 被调整定时器在内部，将定时器取出，重新插入
    else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::del_timer(util_timer *timer)  // 从链表中删除定时器
{
    if (!timer)  // 若定时器为空则直接返回
    {
        return;
    }
    if ((timer == head) && (timer == tail))  // 若链表只有一个定时器
    {
        delete timer;  // 释放内存
        head = NULL;  // 头指针为空
        tail = NULL;  // 尾指针为空
        return;
    }
    if (timer == head)  // 若定时器为头结点
    {
        head = head->next;  // 头指针后移
        head->prev = NULL;  // 新头结点的前驱为空
        delete timer;  // 释放内存
        return;
    }
    if (timer == tail)  // 若定时器为尾结点
    {
        tail = tail->prev;  // 尾指针前移
        tail->next = NULL;  // 新尾结点的后继为空
        delete timer;  // 释放内存
        return;
    }
    timer->prev->next = timer->next;  // 调整前驱的后继指针
    timer->next->prev = timer->prev;  // 调整后继的前驱指针
    delete timer;  // 释放内存
}

// 定时任务处理函数
void sort_timer_lst::tick()
{
    if (!head) 
        return;
 
    // 获取当前时间
    time_t cur = time(NULL);
    util_timer* tmp = head;
 
    // 遍历定时器链表
    while (tmp) {
        // 链表容器为升序排列
        // 当前时间小于定时器超时时间，后面定时器也未到期
        // expire是固定的超时时间，越后面的越晚超时
        if (cur < tmp->expire)
            break;
 
        // 当前定时器到期，则调用回调函数，执行定时事件
        tmp->cb_func(tmp->user_data); // 通过user_data的文件描述符
 
        // 将处理后的定时器，从链表容器删除，并重置头节点
        head = tmp->next;
        if (head) 
            head->prev = NULL;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer* prev = lst_head; // 插入的起点
    util_timer* tmp = prev->next; // 起点下一位置

    // 遍历当前节点之后的链表，按照超时时间，升序插入
    while (tmp) {
        // 由于公有 add_timer() 
        // 此时timer的超时时间，一定 > lst_head
        if (timer->expire < tmp->expire) {
            // 插入 prev 和 prev->next 之间
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break; // 插入完毕
        }
        // prev 和 prev_next 一直往后移动
        prev = tmp; 
        tmp = tmp->next;
    }

    // 如果此时 prev 为尾节点，tmp 为 空
    // timer 超时时间 > 尾节点 超时时间
    if (!tmp) { // timer需要作为新的尾节点
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;  // 初始化时间槽
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);  // 获取文件描述符的标志
    int new_option = old_option | O_NONBLOCK;  // 将标志设置为非阻塞模式
    fcntl(fd, F_SETFL, new_option);  // 设置文件描述符的标志
    return old_option;  // 返回原来的标志
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;  // 设置事件的文件描述符

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;  // 设置事件类型
    else
        event.events = EPOLLIN | EPOLLRDHUP;  // 设置事件类型

    if (one_shot)
        event.events |= EPOLLONESHOT;  // 是否开启EPOLLONESHOT
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);  // 将事件添加到epoll内核事件表中
    setnonblocking(fd);  // 设置文件描述符为非阻塞模式
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;  // 保存原来的errno
    int msg = sig;  // 设置消息为收到的信号值
    send(u_pipefd[1], (char *)&msg, 1, 0);  // 发送消息到管道
    errno = save_errno;  // 恢复errno
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));  // 清空sa结构体
    sa.sa_handler = handler;  // 设置信号处理函数
    if (restart)
        sa.sa_flags |= SA_RESTART;  // 是否重启被中断的系统调用
    sigfillset(&sa.sa_mask);  // 初始化信号屏蔽字
    assert(sigaction(sig, &sa, NULL) != -1);  // 注册信号处理函数
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();  // 触发定时器任务处理
    alarm(m_TIMESLOT);  // 重新定时
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);  // 发送错误信息给客户端
    close(connfd);  // 关闭连接
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);  // 从epoll内核事件表中删除事件
    assert(user_data);  // 断言用户数据有效
    close(user_data->sockfd);  // 关闭socket连接
    http_conn::m_user_count--;  // 用户数减一
}

