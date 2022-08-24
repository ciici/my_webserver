myserver:main.cpp my_epoll.cpp my_socket.cpp my_task.cpp
	g++ $^ -o $@ -pthread

#伪目标，不会生成特定文件
#运行下面规则：make clean
.PHONY:clean
clean:
	rm myserver