#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h> // POSIX 操作系统API
#include <signal.h> // 信号处理
#include <sys/types.h> // 基本系统数据类型
#include <sys/epoll.h> // epoll 操作
#include <fcntl.h> // 文件控制
#include <sys/socket.h> // 套接字接口
#include <netinet/in.h> // IP地址结构
#include <arpa/inet.h> // IP地址转换函数
#include <assert.h> // 断言宏
#include <sys/stat.h> // 文件状态信息
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h> // 内存映射函数
#include <stdarg.h> // 可变参数
#include <errno.h> // 错误码
#include <sys/wait.h> // 进程等待
#include <sys/uio.h> // 向量IO操作
#include <map> // 标准库关联容器

#include "../lock/locker.h" // 线程同步
#include "../CGImysql/sql_connection_pool.h" // 数据库连接池
#include "../timer/lst_timer.h" // 定时器
#include "../log/log.h" // 日志记录

class http_conn 
{
public:

    static const int FILENAME_LEN = 200; 
    static const int READ_BUFFER_SIZE = 2048; // 读缓冲区
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区

    enum METHOD // HTTP 请求方法
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    enum CHECK_STATE // 解析请求的状态
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    enum HTTP_CODE // HTTP 请求处理结果
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    enum LINE_STATUS // 行的解析状态
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:

    http_conn() {} // 构造
    ~http_conn() {} // 析构

public:

    void init(int sockfd, const sockaddr_in &addr, char *, int, int,
            string user, string passwd, string sqlname); // 初始化连接
    void close_conn(bool real_close = true); // 关闭连接
    void process(); // 处理客户请求
    bool read_once(); // 读取浏览器发来的全部数据
    bool write(); // 向写缓冲区写入代发送数据
    sockaddr_in *get_address() // 获取客户端地址
    {
        return &m_address;
    }

    void initmysql_result(connection_pool *connPool); // 初始化数据库连接池
    int timer_flag; // 定时器标志位
    int improv; // 升级标志位

private:

    void init(); // 初始化新的连接
    HTTP_CODE process_read(); // 解析 HTTP 请求
    bool process_write(HTTP_CODE ret); // 填充 HTTP 应答
    HTTP_CODE parse_request_line(char *text); // 分析请求行
    HTTP_CODE parse_headers(char *text); // 分析请求头
    HTTP_CODE parse_content(char *text); // 分析请求内容
    HTTP_CODE do_request(); // 处理 HTTP  请求

    char *get_line() { return m_read_buf + m_start_line; } // 读缓冲区读取一行

    LINE_STATUS parse_line(); // 解析一行内容
    void unmap(); // 关闭连接时，释放映射内存
    bool add_response(const char *format, ...); // 写缓冲区 写入响应报文
    bool add_content(const char *content); // 响应报文正文
    bool add_status_line(int status, const char *titile); // 状态行
    bool add_headers(int content_length); // 报文头部字段
    bool add_content_type(); // content_type 字段
    bool add_content_length(int content_length); 
    bool add_linger(); // Connection 字段
    bool add_blank_line(); // 空行

public:

    static int m_epollfd; // epoll 内核事件表 文件描述符
    static int m_user_count; // 当前连接用户数
    MYSQL* mysql; // 数据库
    int m_state; // 读为 0，写为 1

private:

    int m_sockfd; // HTTP 连接的 socket
    sockaddr_in m_address; 

    char m_read_buf[READ_BUFFER_SIZE]; // 读缓冲区
    long m_read_idx; // 已读入数据位置是最后数据下一位置
    long m_checked_idx; // 当前字符 位于读缓冲区位置
    int m_start_line; // 当前解析行的起始位置

    char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
    int m_write_idx;
    CHECK_STATE m_check_state; // 主状态机 当前状态
    METHOD m_method; // 请求方法

    char m_real_file[FILENAME_LEN]; // 文件完整路径，即 doc_root + m_url
    char *m_url;
    char *m_version; // HTTP 版本号
    char *m_host; // 主机名
    long m_content_length; // HTTP 请求消息体长度
    bool m_linger; // 是否保持连接
    char *m_file_address; // 目标文件被 mmap 到内存的起始位置

    struct stat m_file_stat; // 目标文件状态
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi; // 是否启用 POST
    char *m_string; // 头部字段
    int bytes_to_send; // 未发送字节数
    int bytes_have_send; // 已发送字节数
    char *doc_root; // 网站根目录

    
    map<string, string> m_users; // 用户名 密码
    int m_TRIGMode; // 触发模式
    int m_close_log; // 关闭日志

    char sql_user[100]; // 用户名
    char sql_passwd[100]; // 密码
    char sql_name[100]; // 数据库名
};

#endif