#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cassert>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer {
public:
    WebServer();
    ~WebServer();

    void init(int port, int thread_num, int close_log, int sql_num, 
        std::string user, std::string password, std::string dbname);

    void thread_pool();
    void log_write();
    void sql_pool();
    void event_listen();
    void event_loop();
    bool deal_client_data();
    bool deal_signal(bool &timeout, bool &stop_server);
    void deal_read(int sockfd);
    void deal_write(int sockfd);
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);

public:
    /* 基础连接 */
    int m_port;     // 端口号
    char *m_root;   // 资源文件路径

    int m_pipefd[2];
    http_conn *users;

    /* 日志 */
    int m_close_log;

    /* 数据库相关 */
    Connection_pool *m_conn_pool;
    std::string m_user;         // 登录数据库用户名 
    std::string m_password;     // 登录数据库密码
    std::string m_dbname;       // 数据库名
    int m_sql_num;              // 数据库连接数量

    /* 线程池 */
    int m_thread_num;               // 线程数量，默认设为8
    threadpool<http_conn> *m_pool;  // 线程池

    /* epoll_event相关 */
    int m_epollfd;
    int m_listenfd;
    epoll_event events[MAX_EVENT_NUMBER];
    
    /* 定时器相关 */
    client_data *users_timer;
    Utils utils;
};

#endif