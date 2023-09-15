/**HTTP连接处理类
 * 客户端发出HTTP连接请求
 * 从状态机读取数据，更新自身状态和接收数据，传给主状态机
 * 主状态机根据从状态机状态，更新自身状态（决定响应请求还是继续读取）
 */

#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>

#include "../lock/locker.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;        // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;   // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区的大小

    /* HTTP请求方法 */
    // 项目中只是用 GET 和 POST
    enum METHOD {   
        GET = 0, 
        POST, 
        HEAD, 
        PUT, 
        DELETE, 
        TRACE, 
        OPTIONS, 
        CONNECT,
        PATCH
    };

    /* 主状态机状态，解析客户端请求 */
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,    // 正在解析请求行
        CHECK_STATE_HEADER,             // 正在解析头部字段
        CHECK_STATE_CONTENT             // 正在解析请求体
    };

    /* 服务器处理HTTP请求的结果 */
    enum HTTP_CODE {
        NO_REQUEST,         // 请求不完整，需要继续读取客户数据
        GET_REQUEST,        // 获得了一个完整的客户请求
        BAD_REQUEST,        // 客户请求语法错误
        NO_RESOURCE,        // 服务器没有资源
        FORBIDDEN_REQUEST,  // 客户对资源没有足够的访问权限
        FILE_REQUEST,       // 文件请求,获取文件成功
        INTERNAL_ERROR,     // 服务器内部错误
        CLOSED_CONNECTION   // 客户端关闭连接
    };

    /* 从状态机状态，行的读取状态 */
    enum LINE_STATUS {
        LINE_OK = 0,    // 读取到一个完整的行
        LINE_BAD,       // 行出错
        LINE_OPEN       // 行数据不完整
    };

public:
    http_conn(){}
    ~http_conn(){}

public:
    // 初始化新接受的连接
    void init(int sockfd, const sockaddr_in &addr, char *root, int close_log);
    void close_conn();  // 关闭连接
    void process();     // 处理客户端请求
    bool read();        // 读取客户端发来的全部数据 
    bool write();       // 写入响应报文
    sockaddr_in *get_address()
    {
        return &m_address;
    }

    int timer_flag;
    int improv;

private:
    void init();                        // 初始化连接
    HTTP_CODE process_read();           // 从m_read_buf读取，解析请求报文
    bool process_write(HTTP_CODE ret);  // 向m_write_buf写入响应报文

    /* 解析HTTP请求的函数组，被process_read()调用*/
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    
    char* get_line() { return m_read_buf + m_start_line; }
    // 从状态机读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();

    /* 填充HTTP应答的函数组，被process_write()调用 */
    void unmap();
    bool add_response(const char *format, ... );
    bool add_content(const char *content);
    bool add_content_type();
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;       // 所有socket上的事件注册到同一个epoll内核事件中，因此设置成静态的
    static int m_user_count;    // 统计用户数量

private:
    int m_sockfd;                       // 该HTTP连接的socket
    sockaddr_in m_address;              // 连接客户端的socket地址

    char m_read_buf[READ_BUFFER_SIZE];  // 读缓冲区
    int m_read_idx;                     // 读缓冲区中已读入数据的最后一个字节的下一位置
    int m_checked_idx;                  // 读缓冲区中正在解析的字符的位置
    int m_start_line;                   // 正在解析的行的起始位置，相对读缓冲区起始的偏移量

    CHECK_STATE m_check_state;          // 主状态机当前状态
    METHOD m_method;                    // HTTP连接的请求方法

    char m_real_file[FILENAME_LEN];     // 客户端请求的目标文件的完整路径，等于doc_root + m_url,doc_root是网站的根目录
    char *m_url;                        // 客户端请求的目标文件的文件名
    char *m_version;                    // HTTP协议版本号
    char *m_host;                       // 主机名
    int m_content_length;               // HTTP请求的消息总长度
    bool m_linger;                      // HTTP请求是否要求保持连接

    char m_write_buf[WRITE_BUFFER_SIZE];// 写缓冲区
    int m_write_idx;                    // 写缓冲区中待发送的字节数
    char *m_file_address;               // 客户端请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat;            // 目标文件的状态，可判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2];               // 采用writev执行写操作
    int m_iv_count;                     // 被写内存块的数量
    
    int bytes_to_send;                  // 将要发送的数据的字节数
    int bytes_have_send;                // 已发送的数据的字节数  
    int cgi;                            // 是否启用POST
    char *m_string;                     // 存储请求头数据
    char *doc_root;                     // 资源文件路径
    int m_close_log;                    // 是否关闭日志
};

#endif
