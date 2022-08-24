#include "my_socket.h"

// 接受客户端连接，并打印客户端信息
int my_socket::accept_client() {
    struct sockaddr_in c_address;   // 客户端的socket地址（ip+端口号）
    socklen_t c_addrlength = sizeof(c_address);
    int ret = accept(m_listenfd, (struct sockaddr*)&c_address, &c_addrlength);

    // 输出客户端的信息
    char clientIP[16];
    inet_ntop(AF_INET, &c_address.sin_addr.s_addr, clientIP, sizeof(clientIP));
    unsigned short clientPort = ntohs(c_address.sin_port);
    std::cout << "---------------------------------------------"<< std::endl;
    std::cout << "new connection: client ip is " << clientIP << ", port is " << clientPort << std::endl;

    return ret;       
}

// 创建服务器端用于监听的socket套接字
// 参数：用户指定的端口号
my_socket::my_socket(int port)
{
    // 创建socket（用于监听的套接字），采用TCP流式协议
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if(m_listenfd == -1) {
        perror("socket");
    }

    // 设置端口复用
    int reuse = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    // 创建服务器端的socket地址（ip+端口号）
    struct sockaddr_in s_address;
    s_address.sin_family = AF_INET;
    s_address.sin_addr.s_addr = INADDR_ANY;
    s_address.sin_port = htons(port);

    // 将socket套接字和socket地址进行绑定
    int ret = bind(m_listenfd, (struct sockaddr*)&s_address, sizeof(s_address));
    if(ret == -1) {
        perror("bind");
    }

    // 监听socket上的连接
    int backlog = 10;   // 未连接的和已连接的最大值
    ret = listen(m_listenfd, backlog);
    if(ret == -1) {
        perror("listen");
    }
}

my_socket::~my_socket()
{
    close(m_listenfd);
}