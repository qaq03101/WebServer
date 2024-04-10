#ifndef _CONNECTION_POOL
#define _CONNECTION_POOL

#include <mysql/mysql.h>
#include <string>
#include "../lock/locker.h"
#include <list>
#include <iostream>

using namespace std;

class connection_pool
{
public:
    // 数据库连接
    MYSQL *GetConnection();
    // 释放连接
    bool ReleaseConnection(MYSQL *conn);
    // 获取连接
    int GetFreeConn();
    // 摧毁连接池
    void DestroyPool();

    static connection_pool *GetInstance();

    void init(string url, string User, string PassWord, string DataBaseName, int Port, unsigned int MaxConn);

    connection_pool();
    ~connection_pool();

private:
    unsigned int MaxConn;
    unsigned int CurConn;
    unsigned int FreeConn;

    locker lock;
    // 链表实现链接池
    list<MYSQL *> connList;
    sem reserve;

    // 主机地址
    string url;
    // 数据库端口号
    string Port;
    // 数据库用户名
    string User;
    // 登录密码
    string PassWord;
    // 数据库名
    string DatabaseName;
};


class connectionRAII{

public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};





#endif