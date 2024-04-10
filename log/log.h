#ifndef LOG_H
#define LOG_H

#include <string>
#include <stdio.h>
#include <iostream>
#include "block_queue.h"
#include <string.h>
#include <sys/time.h>
#include <stdarg.h>

class Log
{
public:
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    bool init(const char *file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }

    void write_log(int level, const char *format, ...);
    void flush(void);

private:
    // 路径名
    char dir_name[128];
    // 文件名
    char log_name[128];
    // 日志最大行
    int m_split_lines;
    // 日志缓冲区大小
    int m_log_buf_size;
    // 已使用行
    long long m_count;
    // 按天分文件
    int m_today;
    // 文件指针
    FILE *m_fp;
    // 输出内容
    char *m_buf;
    block_queue<std::string> *m_log_queue;
    // 是否同步
    bool m_is_async;
    locker m_mutex;

    Log();
    // 确保派生类析构函数调用
    virtual ~Log();
    void *async_write_log()
    {
        std::string single_log;

        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }
};


// 通过宏调用日志输出
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format,##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif