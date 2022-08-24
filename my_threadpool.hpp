#ifndef MY_THREADPOOL_H
#define MY_THREADPOOL_H

#include <queue>
#include "my_locker.h"

// 线程池类，将它定义为模板类是为了代码复用，模板参数T是任务类
template<typename T>
class my_threadpool
{
private:
    int m_thread_number;             // 线程的数量
    pthread_t* m_threads;            // 描述线程池的数组，大小为m_thread_number 

    std::queue<T*> m_task_queue;     // 任务队列      
    int m_max_task;                  // 任务队列中最多允许的、等待处理的任务的数量
    my_locker m_queue_locker;        // 保护任务队列的互斥锁
    my_sem m_queue_sem;              // 是否有任务需要处理

    bool m_shutdown;                 // 线程池是否关闭

public:
    my_threadpool(int thread_number = 10, int max_task = 100000);  // 默认创建10个子线程
    ~my_threadpool();
    bool append(T* task);            // 向任务队列中添加一个任务

private:
    static void* worker(void* arg);  // 子线程运行的函数
    void run();                      // 工作线程运行的函数，它不断从任务队列中取出任务并执行之

};

template<typename T>
my_threadpool<T>::my_threadpool(int thread_number, int max_task) :
        m_thread_number(thread_number), m_max_task(max_task), 
        m_shutdown(false), m_threads(NULL) {

    if((thread_number <= 0) || (max_task <= 0) ) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) {
        throw std::exception();
    }

    // 创建 thread_number 个线程，并设置子线程脱离
    for (int i = 0; i < m_thread_number; ++i) {
        if(pthread_create(m_threads+i, NULL, worker, this)) { // worker 为子线程运行的函数
            delete [] m_threads;
            throw std::exception();
        }
        
        if(pthread_detach(m_threads[i])) {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
my_threadpool<T>::~my_threadpool() {
    delete [] m_threads;
    m_shutdown = true;
}

// 向任务队列中添加一个任务
template<typename T>
bool my_threadpool<T>::append(T* task) {
    m_queue_locker.lock();      // 上锁
    if(m_task_queue.size() >= m_max_task) {
        m_queue_locker.unlock();
        return false;
    }
    m_task_queue.push(task);
    m_queue_locker.unlock();    // 解锁
    m_queue_sem.post();         // 增加信号量，使用信号量可以减少工作线程大量无效的轮询
    return true;
}

// 子线程运行的函数
template<typename T>
void* my_threadpool<T>::worker(void* arg)
{
    my_threadpool* pool = (my_threadpool*)arg;
    pool->run();
    return NULL;
}

template<typename T>
void my_threadpool<T>::run() {
    while(!m_shutdown) {
        m_queue_sem.wait();
        m_queue_locker.lock();
        T* task = m_task_queue.front();
        m_task_queue.pop();
        m_queue_locker.unlock();
        if (!task) {
            continue;
        }
        task->process();
    }
}

#endif