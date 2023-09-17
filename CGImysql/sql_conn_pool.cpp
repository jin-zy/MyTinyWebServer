#include "sql_conn_pool.h"

Connection_pool::Connection_pool()
{
    m_CurConn = 0;
    m_FreeConn = 0;
}

Connection_pool *Connection_pool::GetInstance()
{
    static Connection_pool conn_pool;
    return &conn_pool;
}

// 构造初始化
void Connection_pool::init(std::string url, std::string User, std::string PassWord,
    std::string DBName, int port, int MaxConn, int close_log)
{
    m_url = url;
    m_Port = port;
    m_User = User;
    m_PassWord = PassWord;
    m_DataBaseName = DBName;
    m_close_log = close_log;

    for(int i = 0; i < MaxConn; i++) {
        MYSQL *conn = NULL;

        conn = mysql_init(conn);
        if(conn == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        conn = mysql_real_connect(conn, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), port, NULL, 0);
        if(conn == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        connList.push_back(conn);
        ++m_FreeConn;
    }

    reserve = sem(m_FreeConn);

    m_MaxConn = m_FreeConn;
}

// 有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *Connection_pool::Getconnection()
{
    MYSQL *conn = NULL;

    if(0 == connList.size()) {
        return NULL;
    }

    reserve.wait();
    lock.lock();

    conn = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();
    return conn;
}

// 释放当前使用的连接
bool Connection_pool::ReleaseConnection(MYSQL *conn)
{
    if(NULL == conn) {
        return false;
    }

    lock.lock();

    connList.push_back(conn);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();

    reserve.post();
    return true;
}

// 销毁数据库连接池
void Connection_pool::DestroyPool()
{
    lock.lock();
    
    if(connList.size() > 0) {
        std::list<MYSQL *>::iterator it;
        for(it = connList.begin(); it != connList.end(); ++it) {
            MYSQL *conn = *it;
            mysql_close(conn);
        }

        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }

    lock.unlock();
}

// 当前空闲的连接数
int Connection_pool::GetFreeConn()
{
    return this->m_FreeConn;
}

Connection_pool::~Connection_pool()
{
    DestroyPool();
}


connectionRAII::connectionRAII(MYSQL **SQL, Connection_pool *conn_pool)
{
    *SQL = conn_pool->Getconnection();

    connRAII = *SQL;
    poolRAII = conn_pool;

}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(connRAII);
}
