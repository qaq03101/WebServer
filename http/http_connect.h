
#ifndef HTTP_CONN
#define HTTP_CONN

#include <unistd.h>
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


#include "../log/log.h"
#include "../Cmysql/sql_connection_pool.h"


class http_conn
{

public:
    // epoll的fd
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    // 文件名最大长度
    static const int FILENAME_LEN = 200;
    // 读缓存空间
    static const int READ_BUFFER_SIZE = 2048;
    // 写缓存空间
    static const int WRITE_BUFFER_SIZE = 1024;
    // 请求方法
    enum METHOD
    {
        GET = 0,
        POST
    };
    // 主状态机的状态
    enum CHECK_STATE
    {
        // 解析请求行
        CHECK_STATE_REQUSTLINE = 0,
        // 解析请求头
        CHECK_STATE_HEADER,
        // 解析主体
        CHECK_STATE_CONTENT
    };
    // 报文解析结果
    enum HTTP_CODE
    {
        // 请求不完整
        NO_REQUEST,
        // 获取完整请求
        GET_REQUEST,
        // 请求语法有误
        BAD_REQUEST,
        // 请求资源不存在
        NO_RESOURCE,
        // 禁止访问,没有权限
        FORBIDDEN_REQUEST,
        // 可正常访问
        FILE_REQUEST,
        // 服务器内部错误
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    // 从机状态
    enum LINE_STATUS
    {
        // 读取一行
        LINE_OK = 0,
        // 报文语法有误
        LINE_BAD,
        // 读取行不完整
        LINE_OPEN
    };

    http_conn(){};
    ~http_conn(){};
    // 初始化用户连接
    void init(int sockfd, const sockaddr_in &addr);
    // 关闭http连接
    void close_conn(bool real_close = true);
    // 处理请求
    void process();
    // 读取请求报文数据
    bool read_once();
    // 发送报文到B
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    // 初始化数据库的读取表
    void initmysql_result(connection_pool *connpool);
    // 初始化数据库表
    void initresultFile(connection_pool *connPool);

private:
    // socket fd
    int m_sockfd;
    sockaddr_in m_address;
    // 读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    // 读缓冲区大小
    int m_read_idx;
    // 读取缓冲区的位置
    int m_checked_idx;
    // 以解析的字符数
    int m_start_line;
    // 写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 写缓冲长度
    int m_write_idx;

    // 主状态机的状态
    CHECK_STATE m_check_state;
    // 请求方法
    METHOD m_method;

    // 解析请求报文中使用的变量
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;

    // 读取服务器上的文件地址
    char *m_file_address;
    struct stat m_file_stat;
    // io向量机制读写
    struct iovec m_iv[2];
    int m_iv_count;
    // 是否使用的POST
    int cgi;
    // 存储请求头的数据
    char *m_string;
    int bytes_to_send;   // 剩余发送的字节数
    int bytes_have_send; // 已经发送的字节数
    void init();
    // 从缓存区读数据
    HTTP_CODE process_read();
    // 写响应报文数据
    bool process_write(HTTP_CODE ret);
    // 状态机解析请求行
    HTTP_CODE parse_request_line(char *text);
    // 解析请求头
    HTTP_CODE parse_headers(char *text);
    // 解析主体
    HTTP_CODE parse_content(char *text);
    // 生成响应报文
    HTTP_CODE do_request();
    // 读一行
    char *get_line() { return m_read_buf + m_start_line; }
    // 分析行
    LINE_STATUS parse_line();

    void unmap();

    // 根据响应报文格式生成8部分
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_lenght);
    bool add_linger();
    bool add_blank_line();
};


#endif