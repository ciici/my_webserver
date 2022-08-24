#ifndef MY_SOCKET_H
#define MY_SOCKET_H

#include <arpa/inet.h>
#include <cstdio>
#include <iostream>
#include <unistd.h>

// 服务器端的socket封装类
class my_socket
{
private:
    int m_listenfd;                 // 用于监听的socket套接字
public:
    int getListenfd() { return m_listenfd; }
    int accept_client();            // 接受客户端连接
    my_socket(int port = 7000);     // 创建服务器端用于监听的socket套接字
    ~my_socket();
};

#endif