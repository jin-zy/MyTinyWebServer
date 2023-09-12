/**定时器处理非活跃连接
 * 利用alarm函数周期性地触发SIGALRM信号，信号处理函数通过管道通知主循环执行定时器链表上的定时任务
 * - 统一事件源，连接资源、定时事件、超时事件封装为定时器类
 * - 基于升序链表的定时器
 * - 处理非活跃连接
*/

#ifndef LST_TIMER
#define LST_TIMER

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h> 
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

#include "../log/log.h"

// 连接资源结构体
class util_timer;
struct client_data {
    int sockfd;             // socket文件描述符
    sockaddr_in address;    // 客户端socket地址
    util_timer *timer;      // 定时器
};

// 定时器类
class util_timer {
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;                      // 超时时间 

    void (* cb_func)(client_data *);    // 回调函数
    client_data *user_data;             // 连接资源
    util_timer *prev;                   // 前向定时器
    util_timer *next;                   // 后继定时器
};


// 定时器容器 - 带头尾结点的升序双向链表
// 为每一个连接创建定时器，按照升序超时时间排序
class sort_timer_lst {
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick();

private:
    void add_timer(util_timer *timer, util_timer *lst_head);
    
    util_timer *head;
    util_timer *tail;
};

class Utils {
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    // 对文件描述符设置非阻塞
    int setnonblocking(int fd);

    // 向内核事件表注册读事件，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot);

    // 信号处理函数
    static void sig_handler(int sig);

    // 设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

// 定时器回调函数
void cb_func(client_data *user_data);

#endif