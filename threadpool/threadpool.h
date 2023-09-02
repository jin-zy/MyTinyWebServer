/**半同步/半反应堆线程池
 * 使用工作队列解除主线程和工作线程的耦合关系：主线程向工作队列插入任务，工作线程竞争获取任务执行
 * 设计为模板类提高代码复用，模板参数T代表任务类
 */

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"

template <typename T>
class threadpool
{
public:
    threadpool(int thread_num = 8, int max_requests = 10000);
    ~threadpool();
    bool append_p(T *request);

private:
    /* 工作线程运行的主函数，不断从工作队列中获取任务并执行 */
    static void *worker(void *arg);
    void run();

private:
    int m_thread_num;           // 线程池中的线程数
    int m_max_requests;         // 请求队列中允许的最大请求数
    pthread_t *m_threads;       // 线程池数组，大小为m_thread_number
    std::list<T *> m_workqueue; // 请求队列
    locker m_queuelocker;       // 互斥锁，保护请求队列
    sem m_queuestat;            // 信号量，是否有任务需要处理
};

/* 构造函数，创建线程并加入线程池数组m_threads[] */
template <typename T>
threadpool<T>::threadpool(int thread_num, int max_requests)
    : m_thread_num(thread_num), m_max_requests(max_requests), m_threads(NULL)
{
    if(thread_num <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_num];
    if(!m_threads) {
        throw std::exception();
    }

    for(int i = 0; i < thread_num; ++i) {
        if(pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        // 设置脱离线程
        if(pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

/* 析构函数，释放线程池数组内存 */
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

/* 维护请求队列，操作时需要加锁，保证线程安全 */
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // 线程同步，向请求队列添加一个任务
    return true;
}

/* 工作线程主函数 */
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

/* 工作线程实际功能函数，从请求队列中获取任务并执行 */
template<typename T>
void threadpool<T>::run()
{
    while(true) {
        m_queuestat.wait(); // 线程同步，从请求队列获取一个任务
        m_queuelocker.lock();
        if(m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request) continue;
        
        request->process(); // 使用同步I/O模拟Proactor事件处理模式
    }
}

#endif
