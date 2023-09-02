#include "webserver.h"


int main(int argc, char *argv[])
{
    /* 默认设置 */
    int port = 9190;        // 默认端口9190
    int thread_num = 8;  // 默认线程池线程数量8
    int close_log = 0;      // 默认开启日志

    /* 解析命令行参数，自定义配置信息 */
    int opt;
    const char *str = "p:t:l:";
    while((opt = getopt(argc, argv, str)) != -1) {
        switch (opt)
        {
        case 'p': {
            port = atoi(optarg);
            break;
        }
        case 't': {
            thread_num = atoi(optarg);
            break;
        }
        case 'l': {
            close_log = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }

    /* 创建服务器对象 */
    WebServer server;
     
    // 初始化
    server.init(port, thread_num, close_log);
    
    // 日志 
    // server.log_write();  
    
    // 线程池
    server.thread_pool();

    // 监听
    server.event_listen();

    // 运行
    server.event_loop();

    return 0;
}