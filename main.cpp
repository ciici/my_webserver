#include <iostream>
#include "my_threadpool.hpp"
#include "my_task.h"
#include "my_socket.h"
#include "my_epoll.h"

#define DEFAULT_PORT 7000           // 默认服务器的端口号
#define DEFAULT_THREAD_NUM 10       // 默认服务器的端口号
#define DEFAULT_EPOLL_EVENT 10000   // 默认服务器的端口号
#define MAX_FD 65536                // 可以连接的客户端文件描述符的最大个数

int main( int argc, char* argv[] ) {
    std::cout << "提示：可以提供的参数：./" << basename(argv[0]) << " 端口号 线程池中的线程个数 监听的epoll事件个数" << std::endl;

    // 设置端口号
    int port = DEFAULT_PORT;
    if(argc > 1) {       // 有[端口号]参数
        port = atoi(argv[1]);
    }
	std::cout << "running...server http running on port " << port << std::endl;

    // 创建socket（用于监听的套接字）
    my_socket server_socket(port);
    int listenfd = server_socket.getListenfd();

    int thread_num = DEFAULT_THREAD_NUM;
    if(argc > 2) {       // 有[线程个数]参数
        thread_num = atoi(argv[2]);
    }

    // 创建一个线程池
    my_threadpool< my_task >* pool = NULL;
    try {
        pool = new my_threadpool<my_task>(thread_num);
    } catch( ... ) {
        return EXIT_FAILURE;
    }
	std::cout << "running...thread pool creates " <<  thread_num << " threads" << std::endl;

    int epoll_event_num = DEFAULT_EPOLL_EVENT;
    if(argc > 3) {       // 有[epoll事件个数]参数
        epoll_event_num = atoi(argv[3]);
    }

    // 创建一个epoll实例
    my_epoll* server_epoll = NULL;
    try {
        server_epoll = new my_epoll(epoll_event_num);
    } catch( ... ) {
        return EXIT_FAILURE;
    }
	std::cout << "running...epoll monitors " << epoll_event_num << " fd events at most" << std::endl;

    // 将要监听的socket文件描述符添加到epoll对象中，并设置one_shot为false
    server_epoll->addfd(listenfd, false);

    // 每个客户端需要处理的任务
    my_task* users = new my_task[MAX_FD];
    my_task::m_epoll = server_epoll;

    while(true) {
        int number = server_epoll->wait();              // 检测文件描述符的变化
        if (number == -1) {
            std::cout << "epoll failure" << std::endl;
            break;
        }

        for(int i=0; i<number; ++i) {
            int sockfd = server_epoll->getEventfd(i);
            
            if(sockfd == listenfd) {                    // 监听socket发生变化，有新连接到达
                int connfd = server_socket.accept_client(); // 接受客户端连接
                if (connfd < 0) {
                    std::cout << "errno is: " << errno << std::endl;
                    continue;
                }
                if(my_task::m_user_count >= MAX_FD) {   // 已连接客户端超过最大允许值
                    close(connfd);
                    continue;
                }
                users[connfd].init_conn(connfd);        // 初始化新接受的连接，这里为了方便直接将文件描述符作为索引

            } else if(server_epoll->getEventflag(i, EPOLLIN)) {
                if(!users[sockfd].main_read()) {        // 将客户端发送过来的请求数据写入缓冲区中，供子线程处理
                    users[sockfd].close_conn();
                } else {
                    pool->append(users + sockfd);       // 向任务队列中添加一个任务
                }

            } else if(server_epoll->getEventflag(i, EPOLLOUT)) {
                if(!users[sockfd].main_write()) {       // 将子线程处理好的已写入缓冲区中的响应数据发送给客户端
                    users[sockfd].close_conn();
                }

            } else if(server_epoll->getEventflag(i, EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                users[sockfd].close_conn();             // 关闭连接

            }
        }

    }

    delete pool;    

    return EXIT_SUCCESS;
}