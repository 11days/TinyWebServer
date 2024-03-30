// 喳喳辉做项目

// 包含头文件http_conn.h
#include "http_conn.h"

// 包含MySQL的头文件
#include <mysql/mysql.h>
// 包含文件操作相关的头文件
#include <fstream>

// 定义HTTP响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

// 定义互斥锁和用户信息的映射
locker m_lock;
map<string, string> users;

// 初始化MySQL连接, 获取用户信息
void http_conn::initmysql_result(connection_pool *connPool)
{
    // 从连接池中取得一个MySQL连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    // 查询user表中的username和passwd数据
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        // 查询错误时输出日志信息
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 存储查询结果
    MYSQL_RES *result = mysql_store_result(mysql);

    // 获取结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 获取所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 将用户名和密码存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// 对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    // 获取当前文件描述符的选项
    int old_option = fcntl(fd, F_GETFL);
    // 设置新的选项，加上O_NONBLOCK（非阻塞）
    int new_option = old_option | O_NONBLOCK;
    // 更新文件描述符的选项
    fcntl(fd, F_SETFL, new_option);
    return old_option; // 返回旧的选项
}


// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    // 使用ET模式
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT; // 开启EPOLLONESHOT
    // 将文件描述符添加到epoll监听中
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd); // 设置非阻塞
}


// 从内核事件表删除描述符
void removefd(int epollfd, int fd)
{
    // 从epoll监听中删除文件描述符
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd); // 关闭文件描述符
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    // EPOLLONESHOT 模式
    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    // 修改文件描述符的事件
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 初始化静态变量
int http_conn::m_user_count = 0; // HTTP连接数计数
int http_conn::m_epollfd = -1; // epoll文件描述符


// 关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    // 判断是否需要真正关闭连接 以及 套接字描述符是否有效
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd); // 输出关闭的套接字描述符
        removefd(m_epollfd, m_sockfd); // 从 epoll 监听中移除 套接字描述符
        m_sockfd = -1; // 设置套接字描述符为无效值
        m_user_count--; // 客户总量减一
    }
}

// 初始化连接，外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd; // 套接字描述符
    m_address = addr; // 套接字地址

    addfd(m_epollfd, sockfd, true, m_TRIGMode); // 添加 描述符 到 epoll 监听
    m_user_count++; // 客户总量加一

    // 当浏览器出现连接重置时：网站根目录出错 或 http响应格式出错 或 访问的文件内容为空
    doc_root = root; // 设置网站根目录
    m_TRIGMode = TRIGMode; // 设置触发模式
    m_close_log = close_log;

    strcpy(sql_user, user.c_str()); // 复制数据库用户名
    strcpy(sql_passwd, passwd.c_str()); // 复制数据库密码
    strcpy(sql_name, sqlname.c_str()); // 复制数据库名称

    init(); // 调用初始化函数
}

// 初始化新接受的连接，check_state 默认请求行
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    // 清空读写缓冲区和文件名
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}


// 从状态机，用于分析出一行内容
// 返回值为行的状态：LINE_OK, LINE_BAD, LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;  // 临时变量，用于存储当前字符
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)  // 遍历读缓冲区中的字符
    {
        temp = m_read_buf[m_checked_idx];  // 当前位置的字符
        if (temp == '\r')  // 回车符
        {
            if ((m_checked_idx + 1) == m_read_idx)  // 如果回车符后没有更多字符了
                return LINE_OPEN;  // 继续接收字符
            else if (m_read_buf[m_checked_idx + 1] == '\n')  // \r\n 凑齐
            {
                m_read_buf[m_checked_idx++] = '\0';  // 字符串结束符
                m_read_buf[m_checked_idx++] = '\0';  // 字符串结束符
                return LINE_OK;  // 返回行状态为 LINE_OK
            }
            return LINE_BAD;
        }
        else if (temp == '\n')  // 换行符
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;  // 其他情况返回行状态为 LINE_BAD
        }
    }
    return LINE_OPEN;  // 没有找到完整的行，继续接收
}


// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要 一次性 将数据读完
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)  // 如果读取索引超出读缓冲区大小，返回false
    {
        return false;
    }
    int bytes_read = 0;

    // LT 读数据
    if (0 == m_TRIGMode)  // 工作模式 LT
    {
        // 套接字中读取数据
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;  // 更新索引

        if (bytes_read <= 0)  // 如果未成功读取任何数据，返回false
            return false;

        return true;  // 返回true表示读取成功
    }
    // ET 读数据
    else
    {
        while (true)
        {
            // 从套接字中读取数据
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)  // 如果读取出错
            {
                // 暂时不可用 或 阻塞
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0)  // 如果未成功读取任何数据，返回false
            {
                return false;
            }
            m_read_idx += bytes_read;  // 更新索引
        }
        return true;  // 读取成功
    }
}

// 解析http请求行，获得请求方法，目标url，http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    m_url = strpbrk(text, " \t");  // text 中第一个匹配 空格 或 \t 的位置
    if (!m_url) // 未找到
    {
        return BAD_REQUEST; // 请求行：空格 或 \t 分隔
    }
    *m_url++ = '\0';  // 用 \0 分隔 请求方法，目标url，http版本号，并指向下一字符
    char *method = text;

    if (strcasecmp(method, "GET") == 0)  // 如果是GET方法
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)  // 如果是POST方法
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST; 


    m_url += strspn(m_url, " \t");  // 跳过空格和制表符
    m_version = strpbrk(m_url, " \t");  // 第一个匹配 空格 或 \t 的位置

    if (!m_version)
        return BAD_REQUEST;

    *m_version++ = '\0';  // 用 \0 分隔 请求方法，目标url，http版本号，并指向下一字符
    m_version += strspn(m_version, " \t");  // 跳过空格和制表符

    if (strcasecmp(m_version, "HTTP/1.1") != 0)  // 如果不是HTTP/1.1版本
        return BAD_REQUEST;

    if (strncasecmp(m_url, "http://", 7) == 0)  // 如果以http://开头
    {
        m_url += 7;
        m_url = strchr(m_url, '/');  // 在m_url中查找'/'字符的位置
    }

    if (strncasecmp(m_url, "https://", 8) == 0)  // 如果以https://开头
    {
        m_url += 8;
        m_url = strchr(m_url, '/');  // 在m_url中查找'/'字符的位置
    }

    if (!m_url || m_url[0] != '/')  // 如果m_url为空或者第一个字符不是'/'
        return BAD_REQUEST;

    // 当url为/时，显示判断界面
    if (strlen(m_url) == 1)  // 如果url长度为1
        strcat(m_url, "judge.html");  // 追加字符串"judge.html"到m_url末尾

    m_check_state = CHECK_STATE_HEADER;  // 设置检查状态为CHECK_STATE_HEADER

    return NO_REQUEST;  // 返回NO_REQUEST表示解析请求行成功
}

// 解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')  // 如果text为空
    {
        if (m_content_length != 0)  // 内容长度不为0
        {
            m_check_state = CHECK_STATE_CONTENT;  // 设置检查状态
            return NO_REQUEST;  // 解析头部成功
        }
        return GET_REQUEST;  // 获取请求成功
    }

    else if (strncasecmp(text, "Connection:", 11) == 0)  // Connection:开头
    {
        text += 11;  // 跳过Connection:
        text += strspn(text, " \t");  // 跳过空格和制表符
        if (strcasecmp(text, "keep-alive") == 0)  // 如果保持连接
        {
            m_linger = true;  // 设置linger为true
        }
    }

    else if (strncasecmp(text, "Content-length:", 15) == 0)  // Content-length:开头
    {
        text += 15;  // 跳过Content-length:
        text += strspn(text, " \t");  // 跳过空格和制表符
        m_content_length = atol(text); // Content-length 转为长整型存储到 m_content_length
    }
    else if (strncasecmp(text, "Host:", 5) == 0)  // Host:开头
    {
        text += 5;  // 跳过Host:
        text += strspn(text, " \t");  // 跳过空格和制表符
        m_host = text; // Host 字段的值存储到 m_host
    }
    else
    {
        LOG_INFO("oop! unknow header: %s", text); // 记录未知的头部信息
    }
    return NO_REQUEST;  // 解析头部成功
}


// http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0'; // 末尾加 字符串结束符
        // POST请求最后为输入的用户名和密码
        m_string = text; // 读取的内容存入 m_string
        return GET_REQUEST; // 请求完整读取
    }
    return NO_REQUEST; // 请求读取不完整
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    // 循环读取并处理每一行数据，直到无法继续处理
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line(); // 当前行的数据
        m_start_line = m_checked_idx; // 行起始位置
        LOG_INFO("%s", text); // 打印行内容
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text); // 解析请求行
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text); // 解析请求头部
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request(); // 处理请求
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text); // 解析请求内容
            if (ret == GET_REQUEST)
                return do_request(); // 处理请求
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST; // 请求读取不完整
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root); // 网站根目录拷贝到 m_real_file
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/'); // 请求URL最后一个'/'的位置

    // 处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        // 分配内存并拷贝URL内容，构成SQL语句
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        // 处理用户名和密码
        // user=123&password=123
        char name[100], password[100];
        int i;
        // 从 m_string 提取用户名
        // 从 第 6 个字符开始, user= 之后的字符串
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        // m_string 提取密码
        int j = 0;
        // 原来 i 基础上 +10, &password= 之后的字符串
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        // 注册校验
        if (*(p + 1) == '3')
        {
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end()) // 没有重名
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert); // 向数据库插入数据
                users.insert(pair<string, string>(name, password)); // 更新用户信息
                m_lock.unlock();

                if (!res) // 注册成功
                    strcpy(m_url, "/log.html"); // 注册成功跳转到登录页面
                else
                    strcpy(m_url, "/registerError.html"); // 注册失败跳转到注册错误页面
            }
            else
                strcpy(m_url, "/registerError.html"); // 用户已存在，注册失败
        }
        // 如果是登录，直接判断
        // 能找到 且 用户名密码正确，返回1，否则返回0

        // 登录校验
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html"); // 登录成功跳转到欢迎页面
            else
                strcpy(m_url, "/logError.html"); // 登录失败跳转到登录错误页面
        }
    }

    // 注册
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 登录
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 图片
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 视频
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 关注
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else // m_url 复制到 m_real_file 第 len 位置开始
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE; // 文件不存在

    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST; // 无权限访问

    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST; // 请求格式错误

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0); // 将文件映射到内存中
    close(fd);
    return FILE_REQUEST; // 返回FILE_REQUEST表示请求文件内容
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size); // 取消文件内存映射
        m_file_address = 0;
    }
}

bool http_conn::write()
{
    int temp = 0;  // 临时变量，用于存储每次写操作的返回结果

    // 如果没有数据需要发送，则修改文件描述符的事件为可读，并初始化
    if (bytes_to_send == 0)
    {
        // 修改epoll事件，监测可读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();  // 初始化
        return true;
    }

    while (1)  // 循环写操作，直到所有数据发送完毕
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);  // 将待发送数据写入套接字

        // 发送数据出错处理
        if (temp < 0)
        {
            if (errno == EAGAIN)  // 写缓冲区已满，暂时无法发送数据
            {
                // 修改epoll事件，监测可写事件
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();  // 取消内存映射
            return false;
        }

        bytes_have_send += temp;  // 更新已发送字节数
        bytes_to_send -= temp;  // 更新待发送字节数
        if (bytes_have_send >= m_iv[0].iov_len)  // 第一个缓冲区已发送完毕
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else  // 第一个缓冲区未发送完
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        // 数据发送完毕后的处理
        if (bytes_to_send <= 0)
        {
            unmap();  // 取消内存映射
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);  // 修改epoll事件，监测可读事件

            if (m_linger)  // 是否需要保持连接
            {
                init();  // 初始化
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

// 给写缓冲区添加内容
bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)  // 写缓冲区已满
        return false;
    va_list arg_list;
    va_start(arg_list, format);  // 使用可变参数列表
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);  // 格式化输出到写缓冲区
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))  // 格式化输出长度超出写缓冲区剩余空间
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;  // 更新写缓冲区索引
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);  // 记录日志信息

    return true;
}


// 添加状态行到响应中
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加响应头部信息
bool http_conn::add_headers(int content_len)
{
    // 添加Content-Length、Connection和空行到响应中
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

// 添加Content-Length字段到响应中
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

// 添加Content-Type字段到响应中
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 添加Connection字段到响应中
bool http_conn::add_linger()
{
    // 根据是否保持连接添加不同的Connection字段值
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

// 添加空行到响应中
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

// 添加内容到响应中
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}


// 处理写操作，根据不同的HTTP_CODE进行处理
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
        // 内部错误
        add_status_line(500, error_500_title); // 500状态码
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form)) // 对应的页面内容
            return false;
        break;

    case BAD_REQUEST:
        // 请求错误
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;

    case FORBIDDEN_REQUEST:
        // 禁止请求
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;

    case FILE_REQUEST:
        // 文件请求时返回200状态码
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0) // 并根据文件大小处理响应内容
        {
            // 添加头部信息和文件内容到响应中
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            // 文件为空时返回一个简单的HTML页面
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
        break;

    default:
        return false;
    }

    // 设置iov和bytes_to_send，并返回true表示处理成功
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 处理函数，根据读取结果进行处理并修改epoll事件
void http_conn::process()
{
    HTTP_CODE read_ret = process_read(); // 处理读操作
    if (read_ret == NO_REQUEST)
    {
        // 没有请求时修改为监听读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    
    // 处理写操作，并根据结果决定是否关闭连接
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    
    // 修改为监听写事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}





