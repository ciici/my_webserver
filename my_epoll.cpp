#include "my_epoll.h"

my_epoll::my_epoll(int max_event_number) : m_max_event_number(max_event_number), m_events(NULL) 
{
    // 创建一个epoll实例
    m_epollfd = epoll_create(1);   // 参数为任意大于0的值

    //创建监听的epoll事件
    m_events = new epoll_event[m_max_event_number];
    if(!m_events) {
        throw std::exception();
    }
}

my_epoll::~my_epoll() {
    close(m_epollfd);
}

void setnonblocking(int fd) {
    int flag = fcntl( fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}

// 向epoll中添加需要监听的文件描述符fd
void my_epoll::addfd(int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;        // 对面断开连接会触发EPOLLRDHUP
    if(one_shot) 
    {
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd, &event);

    // 设置文件描述符非阻塞
    setnonblocking(fd);  
}

// 向epoll中移除该文件描述符fd
void my_epoll::removefd(int fd) {
    epoll_ctl(m_epollfd, EPOLL_CTL_DEL, fd, 0 );
    close(fd);
}

// 修改文件描述符，重置该socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void my_epoll::modfd(int fd, int flag) {
    epoll_event event;
    event.data.fd = fd;
    event.events = flag | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(m_epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 检测文件描述符的变化
int my_epoll::wait() {
    return epoll_wait(m_epollfd, m_events, m_max_event_number, -1); //阻塞
}

// 获得event[i]指向的文件描述符fd
int my_epoll::getEventfd(int i) {
    return m_events[i].data.fd;
}

// 判断event[i]是否发生了对应状态的事件
bool my_epoll::getEventflag(int i, int flag) {
    return m_events[i].events & flag;
}