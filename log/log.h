/**同步/异步日志系统
 * 主要涉及两个模块，日志模块+阻塞队列模块
 * 加入阻塞队列模块主要是解决异步写入日志做准备
 * - 自定义阻塞队列
 * - 单例模式创建日志
 * - 同步日志
 * - 异步日志
 * - 实现按天、超行分类
*/

#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

class Log {
public:
    // 局部变量懒汉单例模式，C++11后不用加锁也线程安全
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    // 异步写入日志方法，调用私有方法 async_write_log
    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }

    // 可选参数：日志文件、日志缓冲区的大小、最大行数、阻塞队列长度
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, 
        int split_lines = 5000000, int max_queue_size = 0);

    // 将输出内容按标准格式整理
    void write_log(int level, const char *format, ...);

    // 强制刷新缓冲区
    void flush(void);


private:
    Log();
    virtual ~Log();

    // 异步写日志方法
    void *async_write_log()
    {
        std::string single_log;
        // 从阻塞队列中取出一个日志内容，写入文件
        while(m_log_queue->pop(single_log)) {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128];     // 路径名
    char log_name[128];     // 日志文件名
    int m_split_lines;      // 日志最大行数
    int m_log_buf_size;     // 日志缓冲区大小
    long long m_count;      // 日志行数记录
    int m_today;            // 当前时间，按天分类日志
    FILE *m_fp;             // 打开日志的文件指针
    char *m_buf;            // 输出的内容
    int m_close_log;        // 关闭日志
    locker m_mutex;         // 互斥锁
    bool m_is_async;        // 同步标志位
    block_queue<std::string> *m_log_queue;  // 阻塞队列
};


// 宏定义，用于不同类型的日志输出
// 宏定义提供其他程序调用的方法，日志类中的方法不会被直接调用
#define LOG_DEBUG(format, ...) if(0 == m_close_log) { Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush(); }
#define LOG_INFO(format, ...) if(0 == m_close_log) { Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush(); }
#define LOG_WRAN(format, ...) if(0 == m_close_log) { Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush(); }
#define LOG_ERROR(format, ...) if(0 == m_close_log) { Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush(); }

#endif