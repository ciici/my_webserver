#ifndef MY_TASK_H
#define MY_TASK_H

#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include "my_epoll.h"

// 完成HTTP连接的任务
class my_task
{
private:
    int m_sockfd;                           // 该HTTP连接的socket
public:
    static int m_user_count;                // 统计所有HTTP连接的数量
    static my_epoll* m_epoll;               // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的

public:
    // 由主线程执行的函数
    void init_conn(int sockfd);             // 初始化新接受的连接
    void close_conn();                      // 关闭连接
    bool main_read();                       // 将客户端发送过来的数据写入缓冲区中，供子线程处理
    bool main_write();                      // 将子线程处理好的已写入缓冲区中的响应数据发送给客户端
    // 这里模拟 Proactor 模式：使用同步 I/0 方式模拟出 Proactor 模式
    // 原理：主线程执行数据读写操作，读写完成之后，主线程向工作线程通知这一"完成事件”。
    // 工作线程直接获得数据读写的结果，接下来要做的只是对读写的结果进行逻辑处理。

private:
    static const int BUFFER_SIZE = 4096;    // 读写缓冲区的大小
    char m_read_buf[BUFFER_SIZE];           // 读缓冲区
    char m_write_buf[BUFFER_SIZE];          // 写缓冲区'

    int m_read_idx;                         // 读缓冲区的当前位置
    int m_write_idx;                        // 写缓冲区的当前位置
    int m_check_idx;                        // 子线程当前正在分析的字符在读缓冲区中的位置
    int m_send_length;                      // 需要发送给客户端的数据长度
    int m_send_idx;                         // 已经发送给客户端的数据长度

    // 采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    struct iovec m_iv[2];
    int m_iv_count;

private:
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户端请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户端对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        CGI_REQUEST         :   CGI请求
        INTERNAL_ERROR      :   表示服务器内部错误
    */
    enum HTTP_STATE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, CGI_REQUEST, INTERNAL_ERROR };
    
    // HTTP请求方法，这里只支持 GET、POST
    enum METHOD { GET = 0, POST };

public:
    // 由子线程执行的任务函数
    void process();                         // 处理客户端请求主函数
private:
    HTTP_STATE process_read();              // 解析HTTP请求
    bool process_write(HTTP_STATE);         // 生成HTTP响应

    char* parse_one_line();                 // 得到一行HTTP数据
    HTTP_STATE parse_request_line(char* text);// 解析HTTP请求行
    HTTP_STATE parse_headers(char* text);   // 解析HTTP请求头
    void parse_filename();                  // 得到url目标文件路径
    HTTP_STATE execute_request();           // 分析目标文件的属性，得到HTTP状态
    HTTP_STATE execute_cgi();               // 执行cgi动态解析

    bool add_response(const char* format, ...);             // 往写缓冲中写入待发送的数据
    bool add_status_line(int status, const char* title);    // 写入HTTP响应首行
    bool add_headers(int content_len);                      // 写入HTTP响应头
    bool add_content(const char* content);                  // 写入HTTP响应正文

    void init();                            // 初始化一些成员变量

private:   
    METHOD m_method;                        // HTTP请求方法
    char* m_url;                            // 目标URL
    char* m_version;                        // HTTP版本号
    int m_content_length;                   // HTTP请求正文的总长度
    bool m_keep_alive;                      // 是否保持HTTP连接

    static const int FILENAME_LEN = 100;    // 文件名的最大长度
    char m_real_file[FILENAME_LEN];         // HTTP请求目标文件
    struct stat m_file_stat;                // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    char* m_file_address;                   // HTTP请求的目标文件被mmap到内存中的起始位置

    bool m_cgi;                             // 是否需要运行cgi脚本
    char m_cgi_buf[BUFFER_SIZE];            // cgi处理数据的保存位置
    int m_cgi_length;                       // cgi处理数据的长度
    char* m_query_string;                   // cgi环境变量，采用GET时所传输的信息

public:
    my_task() {}
    ~my_task() {}
};

#endif