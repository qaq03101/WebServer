#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"
using namespace std;

connection_pool::connection_pool()
{
    this->CurConn = 0;
    this->FreeConn = 0;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}
int connection_pool::GetFreeConn()
{
	return this->FreeConn;
}
void connection_pool::init(string url, string User, string Password, string Dbname, int Port, unsigned int MaxConn)
{
    this->url = url;
    this->Port = Port;
    this->User = User;
    this->PassWord = Password;
    this->DatabaseName = Dbname;
    lock.lock();
    // 创建链接
    for (int i = 0; i < MaxConn; i++)
    {
        MYSQL *con = NULL;
        con = mysql_init(con);

        if (con == NULL)
        {
            cout << "Error:" << mysql_error(con);
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), Dbname.c_str(), Port, NULL, 0);

        if (con == NULL)
        {
            cout << "Error: " << mysql_error(con);
            exit(1);
        }

        // 更新连接池和空闲连接数量
        connList.push_back(con);
        ++FreeConn;
    }
    // 将信号量初始化为最大连接次数
    reserve = sem(FreeConn);

    this->MaxConn = FreeConn;
    lock.unlock();
}

MYSQL *connection_pool::GetConnection()
{
    MYSQL *con = NULL;
    if (connList.size() == 0)
        return NULL;

    reserve.wait();
    lock.lock();
    con = connList.front();
    connList.pop_front();
    --FreeConn;
    ++CurConn;
    lock.unlock();
    return con;
}

bool connection_pool::ReleaseConnection(MYSQL *con)
{
    if (NULL == con)
        return false;

    lock.lock();

    connList.push_back(con);
    ++FreeConn;
    --CurConn;

    lock.unlock();

    reserve.post();
    return true;
}

void connection_pool::DestroyPool()
{
    lock.lock();
    if (connList.size() > 0)
    {

        list<MYSQL *>::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it)
        {
            MYSQL *con = *it;
            mysql_close(con);
        }
        CurConn = 0;
        FreeConn = 0;
        
        connList.clear();

        lock.unlock();
    }

    lock.unlock();
}

connection_pool *connection_pool::GetInstance(){
    static connection_pool connPool;
    return &connPool;
}

connectionRAII::connectionRAII(MYSQL **con, connection_pool *connPool){
    *con=connPool->GetConnection();
    conRAII=*con;
    poolRAII=connPool;
}

connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);
}