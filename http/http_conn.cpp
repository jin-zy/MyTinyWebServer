#include "http_conn.h"

/* 定义一些HTTP响应的状态信息 */
const char *ok_200_tile = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

std::map<std::string, std::string> users;

void http_conn::init_mysql_res(Connection_pool *conn_pool)
{
    // 从数据库连接池取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysql_conn(&mysql, conn_pool);

    // 在user表中检索浏览器端输入username，password数据
    if(mysql_query(mysql, "SELECT username, password FROM user")) {
        LOG_ERROR("SELECT error: %s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码存入map中
    while(MYSQL_ROW row = mysql_fetch_row(result)) {
        std::string temp1(row[0]);
        std::string temp2(row[1]);
        users[temp1] = temp2;
    }
}

/* 对文件描述符设置非阻塞 */
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/* 向内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT */
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    // 防止一个通信被不同的线程处理
    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}


/* 从内核事件表移除监听的文件描述符 */
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/* 重置EPOLLONESHOT事件，确保下一次可读时能触发EPOLLIN事件 */
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;    // 初始化连接的客户数
int http_conn::m_epollfd = -1;      // 初始化内核事件表

/* 关闭连接，关闭一个连接，客户总数减一 */
void http_conn::close_conn()
{
    if(m_sockfd != -1) {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

/* 初始化连接，外部调用初始化套接字地址 */
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int close_log,
    std::string user, std::string password, std::string dbname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    // 当服务器出现连接重置时，可能时网站根目录出错或者HTTP响应格式出错或者访问的文件内容完全为空
    doc_root = root;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_password, password.c_str());
    strcpy(sql_dbname, dbname.c_str());

    init();
}

void http_conn::init()
{
    bytes_to_send = 0;
    bytes_have_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始状态为检查请求行
    m_linger = false;   // 默认不保持连接，Connection : keep-alive保持连接
    m_method = GET;     // 默认请求方式为GET

    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;


    cgi = 0;
    mysql = NULL;
    m_state = 0;

    timer_flag = 0;
    improv = 0;

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

/* 循环读取客户数据，直到无数据可读或对方关闭连接 */
/* 注意：非阻塞ET模式下，需要一次将数据读完 */
bool http_conn::read()
{
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;
    
    while(true) {
        // 从m_read_buf+m_read_idx索引处开始保存数据，大小是READ_BUF_SIZE-m_read-idx
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1) {
            // 非阻塞ET模式下，需要一次性将数据读完
            if(errno == EAGAIN || errno == EWOULDBLOCK) {   // 没有数据可读
                break;
            }
            return false;
        }
        else if(bytes_read == 0) {  // 对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

/**从状态机，用于解析一行的内容
 * 返回值为行的读取状态：LINE_OK、LINE_BAD、LINE_OPEN
 * 判断依据："\r\n"字符
 */
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(;m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r') {
            if((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0'; // '\r'替换为'\0'
                m_read_buf[m_checked_idx++] = '\0'; // '\n'替换为'\0'
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n') {
            if((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx - 1] = '\0';   // '\r'替换为'\0'
                m_read_buf[m_checked_idx++] = '\0';     // '\n'替换为'\0'
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

/* 解析HTTP请求行，获得请求方法、目标url、HTTP版本号 */
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 示例：GET http://.../judge.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    if(!m_url) {
        return BAD_REQUEST;
    }
    
    // GET\0http://.../judge.html HTTP/1.1
    *m_url++ = '\0';    // 字符串结束符替换空字符
    char *method = text;
    if(strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }
    else if(strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    }
    else {
        return BAD_REQUEST;
    }
    m_url += strspn(m_url, " \t");
    // m_url: "http://.../judge.html HTTP/1.1"
    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    // m_version: "HTTP/1.1"
    if(strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    // m_url: "http://192.168.48.100:9190/index.html\0"
    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if(strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    // m_url: "/judge.html\0"
    if(!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    // 当url为/时，显示静态页面judge.html
    if(strlen(m_url) == 1) {
        strcat(m_url, "judge.html");
    }

    m_check_state = CHECK_STATE_HEADER; // 检查状态切换成检查头
    return NO_REQUEST;
}

/* 解析HTTP请求的头部信息 */
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    // 遇到空行，表示头部信息解析完毕
    if(text[0] == '\0') {
        if(m_content_length != 0) {                 // HTTP请求有消息体
            m_check_state = CHECK_STATE_CONTENT;    // 检查状态切换
            return NO_REQUEST;
        }
        return GET_REQUEST; // 没有消息体，已经获得完整的HTTP请求
    }
    else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }
    else if(strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        LOG_INFO("oop! unknow header: %s", text);
    }
    return NO_REQUEST;
}

/* 判断HTTP请求体是否被完整读入，没有真正解析请求体 */
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if(m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        // POST 请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/* 主状态机，解析请求 */
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
        || ((line_status = parse_line()) == LINE_OK)) {
        // 获取一行信息
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("got 1 http line: %s\n", text);

        // 主状态机的三种状态转移逻辑
        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if(ret == GET_REQUEST)
                    return do_request();
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if(ret == GET_REQUEST)
                    return do_request();
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

/**当获得完整、正确的HTTP请求时，分析目标文件的属性
 * 如果目标文件存在，且不是目录，对请求客户可读，
 * 则使用mmap将其映射到内存地址m_file_address处，通知调用者已获取文件
 */
http_conn::HTTP_CODE http_conn::do_request()
{
    // "/home/jin/MyTinyWebServer/root"
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    
    const char *p = strrchr(m_url, '/');
    /**解析请求资源m_url，'/xxx'由静态页面judge.html中的action设置
     * '/'              GET请求，返回judge.html     访问欢迎页面
     * '/0'             POST请求，返回register.html 注册页面
     * '/1'             POST请求，返回login.html    登录页面
     * '/5'             POST请求，picture.html      图片请求页面
     * '/6'             POST请求，vedio.html        视频请求页面
     * '/7'             POST请求，fans.html         关注页面
     * '/2CGISQL.cgi'   POST请求，登录校验  成功跳转welcome.html,失败跳转loginErr.html
     * '/3CGISQL.cgi'   POST请求，注册校验  成功跳转login.html，失败跳转registerErr.html
    */

    // 处理CGI
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len -1);
        free(m_url_real);

        // 将用户名和密码提取出来
        // user = JIN，password = 123456
        char name[100], password[100];
        int i;
        for(i = 5; m_string[i] != '&'; ++i) {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';

        int j = 0;
        for(i = i + 10; m_string[i] != '\0'; ++i, ++j) {
            password[j] = m_string[i];
        }
        password[j] = '\0';

        // 如果是注册检测，先检测数据库中是否有重名
        // 没有重名的，增加数据
        if(*(p + 1) == '3') {
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, password) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if(users.find(name) == users.end()) {
                m_lock.lock();

                int res = mysql_query(mysql, sql_insert);
                users.insert(std::pair<std::string, std::string>(name, password));
                
                m_lock.unlock();

                if(!res) {
                    strcpy(m_url, "/login.html");
                }
                else {
                    strcpy(m_url, "/registerError.html");
                }
            }
            else {
                strcpy(m_url, "/registerError.html");
            }
        }
        // 如果是登录，直接判断
        // 如果浏览器端输入的用户名和密码可查到返回1，否则返回0
        else if(*(p + 1) == '2') {
            if(users.find(name) != users.end() && users[name] == password) {
                strcpy(m_url, "/welcome.html");
            }
            else {
                strcpy(m_url, "/loginError.html");
            }
        }
    }

    // POST请求，返回register.html 注册页面
    if(*(p + 1) == '0') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    // POST请求，返回login.html    登录页面
    else if(*(p + 1) == '1') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/login.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    // POST请求，picture.html      图片请求页面
    else if(*(p + 1) == '5') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    // POST请求，vedio.html        视频请求页面
    else if(*(p + 1) == '6') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    // POST请求，fans.html         关注页面
    else if(*(p + 1) == '7') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    // GET请求，返回静态页面
    else {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len -1);
    }
    
    // 获取m_real_file文件的相关状态信息：-1失败，0成功
    if(stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if(!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

/* 对内存映射区执行munmap操作 */
void http_conn::unmap()
{
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/* 写HTTP响应 */
bool http_conn::write()
{
    int temp = 0;

    // 待发送字节为0，响应结束
    if(bytes_to_send == 0) {    
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(true) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        
        if(temp < 0) {
            // 如果TCP写缓冲没有空间，等待下一轮EPOLLOUT事件
            // 虽然服务器此时无法立即接收同一客户的下一个请求，但可以保证连接的完整性
            if(errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if(bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if(bytes_to_send <= 0) {
            // 没有数据待发送
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if(m_linger) {
                init();
                return true;
            }
            else {
                return false;
            }
        }
    }
}

/* 往写缓冲中写入待发送数据 */
bool http_conn::add_response(const char *format, ...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }

    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request: %s", m_write_buf);
    return true;
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) &&
        add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

/* 根据服务器处理HTTP请求的结果，决定返回给客户端的内容 */
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)) {
                return false;
            }
            break;

        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)) {
                return false;
            }
            break;        

        case NO_RESOURCE:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)) {
                return false;
            }
            break;

        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)) {
                return false;
            }
            break;
        
        case FILE_REQUEST:
            add_status_line(200, ok_200_tile);
            if(m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len - m_file_stat.st_size;
                m_iv_count = 2;

                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)) {
                    return false;
                }
            }

        default:
            return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

/* 处理HTTP请求的入口函数，由线程池中的工作线程调用 */
void http_conn::process()
{
    // 解析HTTP请求报文
    HTTP_CODE read_ret = process_read();
    // NO_REQUEST，表示请求不完整，需要继续接收请求数据
    if(read_ret == NO_REQUEST) {
        // 注册并监听读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // 生成响应报文
    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }
    // 注册并监听写事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}




