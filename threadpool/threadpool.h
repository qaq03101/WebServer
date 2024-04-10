#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include "../lock/locker.h"
#include "../Cmysql/sql_connection_pool.h"
template <typename T>
class threadpool
{
private:
    // 工作线程函数 worker跳转run
    static void *worker(void *arg);
    void run();

private:
    // 池中线程数
    int m_thread_number;
    // 请求队列的最大请求数
    int m_max_requests;
    // 线程池
    pthread_t *m_threads;
    // 请求队列
    std::list<T *> m_workqueue;
    // 请求队列互斥锁
    locker m_queuelocker;
    // 请求任务处理信号量
    sem m_queuestate;
    // 结束线程
    bool m_stop;
    // 数据库链接池
    connection_pool *m_connPool;
public:
    threadpool(connection_pool *connPool,int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request);
};

template <typename T>  //初始化线程池
threadpool<T>::threadpool(connection_pool *connPool,int thread_number, int max_request) : m_thread_number(thread_number), m_max_requests(max_request), m_stop(false), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_request <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
        //循环创建线程,传递this指针,在worker中通过强转为threadpool跳转run
    for (int i = 0; i < thread_number; i++)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        //分离线程
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

template <typename T>   //请求队列添加请求
bool threadpool<T>::append(T *request)
{
    //防止竞争请求队列
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    //唤醒线程处理请求
    m_queuestate.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg){
    threadpool *pool=(threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run(){
    while(!m_stop){
        //等待信号
        m_queuestate.wait();
        //防止竞争
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request=m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request){
            continue;
        }
        //为请求添加数据库连接
        connectionRAII mysqlcon(&request->mysql, m_connPool);
        //处理
        request->process();

    }

}

#endif