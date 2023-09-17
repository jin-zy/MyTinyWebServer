#ifndef SQL_CONN_POOL
#define SQL_CONN_POOL

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string.h>
#include <errno.h>
#include <list>
#include <string>
#include <pthread.h>
#include <mysql/mysql.h>

#include "../lock/locker.h"
#include "../log/log.h"

class Connection_pool
{
public:
    MYSQL *Getconnection();                 // 获取数据库连接
    bool ReleaseConnection(MYSQL *conn);    // 释放连接
    int GetFreeConn();                      // 获取连接
    void DestroyPool();                     // 销毁所有的连接

    // 单例模式
    static Connection_pool *GetInstance();

    // 构造初始化
    void init(std::string url, std::string User, std::string PassWord,
        std::string DBName, int port, int MaxConn, int close_log);

public:
    std::string m_url;          // 主机地址
    std::string m_Port;         // 数据库端口号
    std::string m_User;         // 登录数据库用户名
    std::string m_PassWord;     // 登录数据库密码
    std::string m_DataBaseName; // 使用数据库名
    int m_close_log;            // 日志开关

private:
    Connection_pool();
    ~Connection_pool();

    int m_MaxConn;  // 最大连接数
    int m_CurConn;  // 当前已使用的连接数
    int m_FreeConn; // 当前空闲的连接数

    locker lock;
    sem reserve;
    std::list<MYSQL *> connList;    // 连接池   
};

class connectionRAII {
public:
    connectionRAII(MYSQL **conn, Connection_pool *conn_pool);
    ~connectionRAII();

private:
    MYSQL *connRAII;
    Connection_pool *poolRAII;
};

#endif