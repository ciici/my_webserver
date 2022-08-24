#include "my_task.h"

// 所有的客户数
int my_task::m_user_count = 0;
my_epoll* my_task::m_epoll = NULL;

// 网站的根目录
const char* doc_root = "./resources";
const char* default_url = "/index.html";

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 初始化新接受的连接
void my_task::init_conn(int sockfd) {
    m_sockfd = sockfd;
    m_epoll->addfd(m_sockfd, true);   // 将与客户端通信的socket添加到epoll对象中
    ++m_user_count;
    init();
}

// 关闭连接
void my_task::close_conn() {
    m_epoll->removefd(m_sockfd);     // 从epoll对象中删除该socket
    m_sockfd = -1;
    --m_user_count;
}

// 初始化一些成员变量
void my_task::init() {
    bzero(m_read_buf, BUFFER_SIZE);
    bzero(m_write_buf, BUFFER_SIZE);
    m_read_idx = 0;
    m_write_idx = 0;
    m_check_idx = 0;
    m_send_length = 0;
    m_send_idx = 0;
    m_iv_count = 0;

    m_content_length = 0;
    m_keep_alive = false;
    bzero(m_real_file, FILENAME_LEN);
    m_cgi = false;
    bzero(m_cgi_buf, BUFFER_SIZE);
    m_cgi_length = 0;
}

// 将客户端发送过来的数据写入缓冲区中，供子线程处理
bool my_task::main_read() {
    if(m_read_idx >= BUFFER_SIZE) {
        return false;
    }
    while(true) {
        // 从 m_read_buf + m_read_idx 开始读数据，大小是 BUFFER_SIZE - m_read_idx
        int bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, BUFFER_SIZE - m_read_idx, 0 );
        if (bytes_read == -1) {
            if( errno == EAGAIN || errno == EWOULDBLOCK ) { // 没有数据
                break;
            }
            return false;   
        } else if (bytes_read == 0) {   // 对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

// 将子线程处理好的已写入缓冲区中的响应数据发送给客户端
bool my_task::main_write() {
    int temp = 0;
    
    if (m_send_length == 0) {               // 将要发送的字节为0，这一次响应结束
        m_epoll->modfd(m_sockfd, EPOLLIN);  // 重新监听读事件
        init();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1) {
            if(errno == EAGAIN) {
                // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
                // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
                m_epoll->modfd(m_sockfd, EPOLLOUT);
                return true;
            } else {
                munmap(m_file_address, m_file_stat.st_size);
                m_file_address = NULL;
                return false;
            }
        }

        if(m_send_length <= 0)  // 没有数据要发送了
        {
            munmap(m_file_address, m_file_stat.st_size);
            m_file_address = NULL;
            m_epoll->modfd(m_sockfd, EPOLLIN);

            if(m_keep_alive)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }

        m_send_idx += temp;
        m_send_length -= temp;

        // 写入数据的“结束点”可能位于一个iovec的中间某个位置，因此需要调整临界iovec的iov_base和iov_len。
        if(m_send_idx >= m_iv[0].iov_len)
        {
            m_iv[1].iov_base = m_file_address + (m_send_idx - m_iv[0].iov_len);
            m_iv[1].iov_len = m_send_length;
            m_iv[0].iov_len = 0;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + m_send_idx;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }
    }
}

// 处理客户端请求主函数
void my_task::process() {
    // 解析HTTP请求
    HTTP_STATE read_ret = process_read();
    if(read_ret == NO_REQUEST) {             // 请求数据不完整
        m_epoll->modfd(m_sockfd, EPOLLIN);   // 重新监听读事件
        return;
    }

    // 生成HTTP响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    m_epoll->modfd(m_sockfd, EPOLLOUT);     // 监听写事件
}

// 解析HTTP请求
my_task::HTTP_STATE my_task::process_read() {
    char* text = parse_one_line();          // 得到一行HTTP数据
    if(!text) {
        return NO_REQUEST;
    }
    std::cout << "---------------------------------------------"<< std::endl;
    std::cout << "获得HTTP请求行 " << text << std::endl;
    int ret = parse_request_line(text);     // 解析HTTP请求行
    if(ret == BAD_REQUEST)
        return BAD_REQUEST;

    while(ret == NO_REQUEST)
    {
        text = parse_one_line();
        // std::cout << "获得HTTP头 " << text << std::endl;
        ret = parse_headers(text);
    }

    if(m_content_length != 0) {             // 如果HTTP请求有消息体
        text = m_read_buf + m_check_idx;
        std::cout << "获得HTTP消息体 " << text << std::endl;
    }

    // 得到url目标文件路径
    parse_filename();

    // 已经得到了一个完整的HTTP请求
    if(!m_cgi) {
        return execute_request();
    } else {
        return execute_cgi();
    }
}

// 得到一行HTTP数据
char* my_task::parse_one_line() {
    int idx = m_check_idx;
    for ( ; m_check_idx < m_read_idx; ++m_check_idx) {
        char temp = m_read_buf[m_check_idx];
        if (temp == '\r') {
            if (m_check_idx + 1 == m_read_idx) {
                return NULL;
            } else if (m_read_buf[m_check_idx + 1] == '\n') {
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return m_read_buf + idx;
            }
            return NULL;
        }
    }
    return NULL;
}

// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
// 返回值：BAD_REQUEST：客户端语法错误；NO_REQUEST：请求行解析完毕，继续解析请求头
my_task::HTTP_STATE my_task::parse_request_line(char* text) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if(!m_url) { 
        return BAD_REQUEST;
    }

    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符
    char* method = text;
    if(strcasecmp(method, "GET") == 0) { // 忽略大小写比较
        m_method = GET;
    } else if(strcasecmp(method, "POST") == 0) {
        m_method = POST;
        m_cgi = true;
    } else {
        return BAD_REQUEST;
    }

    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if(strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // http://192.168.110.129:10000/index.html
    if(strncasecmp(m_url, "http://", 7) == 0) {   
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/');
    }
    if(!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    
    //GET请求url可能会带有?,有查询参数
    if(strcasecmp(method, "GET") == 0)
    {
        m_query_string = m_url;
        while ((*m_query_string != '?') && (*m_query_string != '\0'))
            m_query_string++;
        
        /* 如果有?表明是动态请求, 开启cgi */
        if (*m_query_string == '?')
        {
            m_cgi = true;
            *m_query_string = '\0';
            m_query_string++;
        }
    }

    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
// 返回值：GET_REQUEST：请求头解析完毕；NO_REQUEST：请求头未解析完毕
my_task::HTTP_STATE my_task::parse_headers(char* text) {
    if(text[0] == '\0') {             // 遇到空行，表示头部字段解析完毕
        return GET_REQUEST;
    } else if(strncasecmp( text, "Connection:", 11 ) == 0) {
        // 处理Connection 头部字段 Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if (strcasecmp( text, "keep-alive" ) == 0) {
            m_keep_alive = true;
        }
    } else if(strncasecmp( text, "Content-Length:", 15 ) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    }
    return NO_REQUEST;
}

// 当得到一个完整、正确的HTTP请求时，我们就提取目标文件的路径，保存到m_real_file中
void my_task::parse_filename() {
    strcpy(m_real_file, doc_root);    // "./resources"
    int len = strlen(doc_root);
    if(strcmp(m_url, "/") == 0) {
        strncpy(m_real_file + len, default_url, FILENAME_LEN - len - 1 );
    } else {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    }
}

// 分析目标文件的属性，返回HTTP状态
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
my_task::HTTP_STATE my_task::execute_request() {
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if(stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if(!(m_file_stat.st_mode & S_IROTH)) {     // 如果没有读权限
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char*)mmap(NULL, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(m_file_address == MAP_FAILED) {
        perror("mmap");
        return INTERNAL_ERROR;
    }
    close(fd);
    return FILE_REQUEST;
}

// 执行cgi动态解析
my_task::HTTP_STATE my_task::execute_cgi()
{
    int cgi_output[2];
    int cgi_input[2];
    if(pipe(cgi_output) < 0 || pipe(cgi_input) < 0) { 
        return INTERNAL_ERROR;
    }

    int pid;
    if((pid = fork()) < 0) {
        return INTERNAL_ERROR;
    }
    if(pid == 0) {              // 子进程: 运行CGI 脚本 
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], 1); // 将cgi_output中的写通道重定向到标准输出
        dup2(cgi_input[0], 0);  // 将cgi_input中的读通道重定向到标准输入

        close(cgi_output[0]);   // 关闭了cgi_output中的读通道
        close(cgi_input[1]);    // 关闭了cgi_input中的写通道

        // 设置CGI程序环境变量
        if(m_method == GET) {     
            sprintf(meth_env, "REQUEST_METHOD=GET");
            putenv(meth_env);
            sprintf(query_env, "QUERY_STRING=%s", m_query_string);
            putenv(query_env);
        }
        else if(m_method == POST) {
            // 用POST方法，那么客户端来的用户数据将存放在CGI进程的标准输入中
            // 同时将用户数据的长度赋予环境变量中的CONTENT_LENGTH
            sprintf(meth_env, "REQUEST_METHOD=POST");
            putenv(meth_env);
            sprintf(length_env, "CONTENT_LENGTH=%d", m_content_length);
            putenv(length_env);
        }

        execl(m_real_file, m_real_file, NULL);  // 执行CGI脚本
        exit(0);
    } 
    else {  
        close(cgi_output[1]);
        close(cgi_input[0]);

        if(m_method == POST) {
            write(cgi_input[1], m_read_buf + m_check_idx, m_content_length + 1);
        }

		// 读取cgi脚本返回数据
		m_cgi_length = read(cgi_output[0], m_cgi_buf, BUFFER_SIZE);

		// 运行结束关闭
		close(cgi_output[0]);
		close(cgi_input[1]);

        // 回收子进程
        int status;
		waitpid(pid, &status, 0);
	}
    return CGI_REQUEST;
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool my_task::process_write(HTTP_STATE ret) {
    switch(ret)
    {
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line(400, error_400_title );
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            m_send_length = m_write_idx + m_file_stat.st_size;
            return true;
        case CGI_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_cgi_buf;
            m_iv[1].iov_len = m_cgi_length;
            m_iv_count = 2;
            m_send_length = m_write_idx + m_cgi_length;
            return true;
        default:
            return false;
    }
    
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    m_send_length = m_write_idx;
    return true;
}

// 写入HTTP响应首行
bool my_task::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title );
}

// 写入HTTP响应头
bool my_task::add_headers(int content_len) {
    add_response("Content-Length: %d\r\n", content_len);
    add_response("Content-Type:%s\r\n", "text/html");
    add_response("Connection: %s\r\n", (m_keep_alive == true) ? "keep-alive" : "close");
    add_response("%s", "\r\n");
    return true;
}

// 写入HTTP响应正文
bool my_task::add_content(const char* content) {
    return add_response("%s", content);
}

// 往写缓冲中写入待发送的数据
bool my_task::add_response(const char* format, ...) {
    if(m_write_idx >= BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= (BUFFER_SIZE - 1 - m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}