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
    bool append_p(T *request);      // 向请求队列中添加任务

private:
    /* 工作线程运行的主函数，不断从工作队列中获取任务并执行 */
    static void *worker(void *arg); // 需要设置成静态成员函数
    void run();

private:
    int m_thread_num;               // 线程池中的线程数
    int m_max_requests;             // 请求队列中允许的最大请求数
    pthread_t *m_threads;           // 线程池数组，大小为m_thread_number
    std::list<T *> m_workqueue;     // 请求队列
    locker m_queuelocker;           // 互斥锁，保护请求队列
    sem m_queuestat;                // 信号量，是否有任务需要处理
    bool m_stop;                    // 是否结束线程
};

/* 构造函数，创建线程并加入线程池数组m_threads[] */
template <typename T>
threadpool<T>::threadpool(int thread_num, int max_requests)
    : m_thread_num(thread_num), m_max_requests(max_requests), m_threads(NULL), m_stop(false)
{
    if(thread_num <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_num];
    if(!m_threads) {
        throw std::exception();
    }

    for(int i = 0; i < thread_num; ++i) {
        // 循环创建 thread_num 个线程，并指定线程处理函数 worker()
        if(pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        // 将线程分离后，不用单独对工作线程进行回收
        if(pthread_detach(m_threads[i]) != 0) {
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
    m_stop = true;
}

/* 向请求队列添加入任务，操作时需要加锁，保证线程安全 */
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    // 添加任务
    m_workqueue.push_back(request);
    m_queuelocker.unlock();

    // 信号量提醒有任务要处理
    m_queuestat.post();
    return true;
}

/* 工作线程处理函数 */
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    // 将参数强转为线程池类，调用私有成员方法
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

/* 工作线程实际功能函数，从请求队列中获取任务并处理 */
template<typename T>
void threadpool<T>::run()
{
    while(!m_stop) {
        // 信号量等待
        m_queuestat.wait();

        // 被唤醒后先加互斥锁（竞争获取任务）
        m_queuelocker.lock();
        if(m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }

        // 从请求队列中取出一个任务
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request) continue;
        
        // http类中的方法
        request->process();
    }
}

#endif
