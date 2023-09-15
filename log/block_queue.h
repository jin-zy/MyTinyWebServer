// 循环数组实现的阻塞队列
// m_back = (m_back + 1) % m_max_size;
// 线程安全，每个操作前先加锁，操作完成后解锁

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"

template <class T>
class block_queue {
public:
    // 默认构造
    block_queue(int max_size = 1000)
    {
        if(max_size <= 0) {
            exit(-1);
        }

        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    ~block_queue()
    {
        m_mutex.lock();
        if(m_array != NULL) {
            delete[] m_array;
        }
        m_mutex.unlock();
    }

    void clear()
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    // 判断队列是否满
    bool full()
    {
        m_mutex.lock();
        if(m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 判断队列是否空
    bool empty()
    {
        m_mutex.lock();
        if(0 == m_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }


    // 返回队首元素
    bool front(T &value)
    {
        m_mutex.lock();
        if(0 == m_size) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }

    // 返回队尾元素
    bool back(T &value)
    {
        m_mutex.lock();
        if(0 == m_sie) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    // 获取队列长度
    int size()
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }

    // 获取队列最大长度
    int max_size()
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp; 
    }

    // 向队列中添加元素，当有元素添加进队列，相对于生产者生产了一个元素
    // 将所有使用队列的线程唤醒，若当前没有线程等待条件变量，则唤醒没有意义
    bool push(const T &item)
    {
        m_mutex.lock();
        
        // 阻塞队列满，唤醒线程处理
        if(m_size >= m_max_size) {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }
        
        // 将新增元素放入循环数组中对应的位置
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;

        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    // 如果当前队列中没有元素，将会等待条件变量
    bool pop(T &item)
    {
        m_mutex.lock();

        // 多个消费者，使用 while() 循环争抢资源
        while(m_size <= 0) {
            // 当重新抢到互斥锁，wait() 返回为 0
            if(!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    // 超时处理，项目中未使用
    // wait() 的基础上增加等待时间，指定时间内抢到互斥锁
    bool pop(T &item, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);

        m_mutex.lock();
        if(m_size <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if(!m_cond.timewait(m_mutex.get(), t)) {
                m_mutex.unlock();
                return false;
            }
        }

        if(m_size <= 0) {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }
    

private:
    locker m_mutex; // 互斥锁
    cond m_cond;    // 条件变量

    T *m_array;     // 阻塞队列
    int m_size;
    int m_max_size;
    int m_front;
    int m_back; 
};


#endif