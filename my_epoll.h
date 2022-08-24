#ifndef MY_EPOLL_H
#define MY_EPOLL_H

#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <exception>
#include <fcntl.h>

class my_epoll
{
private:
    int m_epollfd;
    epoll_event *m_events;      // epoll事件数组
    int m_max_event_number;     // 监听的epoll事件的最大个数
    
public:
    int getEpollfd() { return m_epollfd; }

    void addfd(int fd, bool one_shot);      // 向epoll中添加需要监听的文件描述符fd
    void removefd(int fd);                  // 向epoll中移除该文件描述符fd
    void modfd(int fd, int flag);           // 修改文件描述符fd中的监听状态为flag

    int wait();                             // 检测文件描述符的变化
    int getEventfd(int i);                  // 获得event[i]指向的文件描述符fd
    bool getEventflag(int i, int flag);     // 判断event[i]是否发生了对应状态flag的事件

    my_epoll(int max_event_number = 10000); // 默认监听的最大的事件数量为10000
    ~my_epoll();
};

#endif