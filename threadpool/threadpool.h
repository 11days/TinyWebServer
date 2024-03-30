// 喳喳辉做项目

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"


// 线程池 定义
template <typename T>
class threadpool
{
public:
    // thread_number 线程数量，max_requests 请求队列中最多允许的、等待处理请求的数量
    // connPool 数据库连接池 指针
    threadpool(int actor_model, connection_pool *connPool, 
            int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state); 
    bool append_p(T *request); // 请求队列 插入任务请求

private:
    // 工作线程运行的函数
    // 不断从工作队列取出任务  并执行
    static void *worker(void *arg);

    void run();

private:
    int m_thread_number;        //线程数
    int m_max_requests;         //请求队列最大请求数
    pthread_t *m_threads;       //描述线程池的数组，大小 m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //模型切换
};

// 创建和回收
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, 
                        int thread_number, int max_requests) 
                        : 
                        m_actor_model(actor_model),
                        m_thread_number(thread_number), 
                        m_max_requests(max_requests), 
                        m_threads(NULL),
                        m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();

    // 线程 id 初始化
    m_threads = new pthread_t[m_thread_number];

    if (!m_threads)
        throw std::exception();

    for (int i = 0; i < thread_number; ++i)
    {
        // 循环创建线程，并将工作线程按要求运行
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }

        // 线程分离后，不用单独回收工作线程
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 析构
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}


// 添加任务
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();

    // 根据硬件，预先设置请求队列最大值
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }

    request->m_state = state;

    // 添加任务
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// 添加任务（无状态）
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();

    // 设置队列最大值
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }

    // 添加任务
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// 内部访问私有 run(), 进行线程处理
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    // 参数强转线程池类
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

// 工作线程 从 请求队列 取出任务处理（线程同步）
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        // 信号量等待
        m_queuestat.wait();

        // 唤醒后先加锁
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }

        // 请求队列第一个任务
        T *request = m_workqueue.front();
        // 任务从请求队列删除
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (!request) // 任务为空
            continue;

        if (1 == m_actor_model) // 根据变量值，确定不同模型
        {
            if (0 == request->m_state)
            {
                if (request->read_once()) // 从连接读取数据
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process(); // 处理请求
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                if (request->write()) // 向连接写入数据
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else // m_actor_model 不为 1
        {
            // 获取连接
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            // 处理请求
            request->process();
        }
    }
}
#endif