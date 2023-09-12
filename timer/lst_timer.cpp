#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}

sort_timer_lst::~sort_timer_lst()
{
    // 销毁链表
    util_timer *tmp = head;
    while(tmp) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer)
{
    if(!timer) {
        return;
    }
    if(!head) {
        head = tail = timer;
        return;
    }
    // 如果新的定时器超时时间小于当前头结点，将其设置为头节点。
    if(timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    // 否则调用私有成员，调整内部结点
    add_timer(timer, head);
}

void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if(!timer) {
        return;
    }
    util_timer *tmp = timer->next;

    // 定时器在链表尾部，或者调整超时时间后仍小于下一个定时器的超时时间，不用调整
    if(!tmp || (timer->expire < tmp->expire)) {
        return;
    }

    // 定时器是链表头结点，取出后重新插入
    if(timer == head) {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    // 定时器是内部结点，取出后重新插入
    else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::del_timer(util_timer *timer)
{
    if(!timer) {
        return;
    }
    if((timer == head) && (timer == tail)) {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if(timer == head) {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if(timer == tail) {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;

    // 遍历当前结点之后的链表，按超时时间找到目标定时器的位置
    while(tmp) {
        if(timer->expire < tmp->expire) {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    // 遍历完，目标定时器放到尾结点位置
    if(!tmp) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

// 定时任务处理函数
void sort_timer_lst::tick()
{
    if(!head) {
        return;
    }

    // 获取当前时间
    time_t cur = time(NULL);

    // 遍历定时器链表
    util_timer *tmp = head;
    while(tmp) {
        if(cur < tmp->expire) {
            break;
        }
        // 当前定时器到期，调用回调函数，执行定时事件
        tmp->cb_func(tmp->user_data);

        // 将处理完的定时器从链表容器中删除，重置头结点
        head = tmp->next;
        if(head) {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

// 对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 向内核事件表注册读事件，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;

    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 信号处理函数
void Utils::sig_handler(int sig)
{
    // 为保证函数的可重入性，保留原先的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

// 设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    bzero(&sa, sizeof(sa));
    sa.sa_handler = handler;
    if(restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data)
{
    // 删除非活动连接在socket上的注册事件
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    // 关闭文件描述符
    close(user_data->sockfd);
    // 减少连接数
    http_conn::m_user_count--;
}

