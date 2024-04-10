#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./http/http_connect.h"
#include "./timer/timer.h"
#include "./Cmysql/sql_connection_pool.h"

#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <cassert>
#include <cstring>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_FD 65535           // 最大文件描述符
#define MAX_EVENT_NUMBER 10000 // 最大事件数
#define TIMESLOT 500            // 最小超时单位
// 异步同步日志
#define SYNLOG
// #define ASYLOG

// #define listenfdET  //ET LT触发方式
#define listenfdLT
// 事件描述符
static int epollfd = 0;
static sort_timer_lst timer_lst;
static int pipefd[2];

extern int addfd(int epollfd, int fd, bool one_shot);
extern int remove(int epollfd, int fd);
extern int setnonblocking(int fd);

// 定时器处理任务,重新定时不断触发SIGALRM信号
void timer_handler()
{
    timer_lst.tick();
    alarm(TIMESLOT);
}

// 定时器回调函数
void cb_func(client_data *user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}

// 信号处理函数
void sig_handler(int sig)
{
    // 可重入性,保留进入时的errno状态
    int save_errno = errno;
    int msg = sig;

    send(pipefd[1], (char *)&msg, 1, 0);

    errno = save_errno;
}
// 添加信号
void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    // 仅仅发送信号,不处理
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;

    // 添加信号到信号集
    sigfillset(&sa.sa_mask);

    assert(sigaction(sig, &sa, NULL) != -1);
}

// 发送错误
void show_error(int connfd, const char *info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char *argv[])
{
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 8); // 异步日志模型
#endif

#ifdef SYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 0); // 同步日志模型
#endif
    //端口号
    int port = 9897;

    addsig(SIGPIPE, SIG_IGN);
    // 数据库链接池
    connection_pool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "QAQ", "12138", "people", 3306, 10);

    // 线程池创建
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>(connPool);
    }
    catch (...)
    {
        return 1;
    }

    // 服务器最大的HTTP连接
    http_conn *users = new http_conn[MAX_FD];
    // 初始化数据库读取表
    users->initmysql_result(connPool);
    // 内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(10);
    assert(epollfd != -1);
    // 套接字创建
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0;
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    // 复用端口
    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 10);
    assert(ret >= 0);
    // 添加http请求事件
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;
    // 创建管道

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);

    bool timeout = false;
    bool stop_server = false;
    // 主线程的超时信号
    alarm(TIMESLOT);

    client_data *users_timer = new client_data[MAX_FD];

    while (!stop_server)
    {
        // 等待事件发生
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            break;
        }
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            // 处理新的连接
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_addr;
                socklen_t client_addrlen = sizeof(client_addr);
#ifdef listenfdLT
                int connfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_addrlen);
                if (connfd < 0)
                {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internet server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }
                users[connfd].init(connfd, client_addr);

                // 为客户连接请求设置定时器
                users_timer[connfd].address = client_addr;
                users_timer[connfd].sockfd = connfd;
                util_timer *timer = new util_timer;
                timer->user_data = &users_timer[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                timer_lst.add_timer(timer);

#endif
#ifdef listenfdET
                while (1)
                {
                    int connfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_addrlen);
                    if (connfd < 0)
                    {
                        break;
                    }
                    if (http_conn::m_user_cont >= MAX_FD)
                    {
                        show_error(connfd, "Internet server busy") break;
                    }
                    users[connfd].init(connfd, client_addr);
                    // 为客户连接请求设置定时器
                    users_timer[connfd].address = client_addr;
                    users_timer[connfd].sockfd = connfd;
                    util_timer *timer = new util_timer;
                    timer->user_data = &users_timer[connfd];
                    timer->cb_func = cb_func;
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 * TIMESLOT;
                    users_timer[connfd].timer = timer;
                    timer_lst.add_timer(timer);
                }
                continue;
#endif
            }
            // 异常
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务器关闭连接
                util_timer *timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);
                if (timer)
                    timer_lst.del_timer(timer);
            }
            // 处理信号
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                // 读出信号
                ret = recv(pipefd[0], signals, sizeof(signals), 0);

                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; i++)
                    {
                        switch (signals[i])
                        {
                        case SIGABRT:
                            timeout = true;
                            break;
                        case SIGTERM:
                            stop_server = true;
                            break;
                        }
                    }
                }
            }
            // 处理客户连接接受的数据
            else if (events[i].events & EPOLLIN)
            {
                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].read_once())
                {
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                    // 主线程读后,工作线程处理数据
                    pool->append(users + sockfd);

                    // 延迟该请求的超时时间
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {

                    // 关闭服务器
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                        timer_lst.del_timer(timer);
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].write())
                {
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete pool;
    return 0;
}