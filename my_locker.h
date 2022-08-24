#ifndef MY_LOCKER_H
#define MY_LOCKER_H

// 线程同步机制封装类
#include <pthread.h>
#include <exception>
#include <semaphore.h>

// 互斥锁类
class my_locker {
public:
    my_locker() {                   // 互斥锁初始化
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    ~my_locker() {                  // 释放互斥锁
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock() {                   // 上锁
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock() {                 // 解锁
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

private:
    pthread_mutex_t m_mutex;        // 互斥锁
};

// 信号量类
class my_sem {
public:
    my_sem() {                      // 初始化信号量的值为0
        if( sem_init( &m_sem, 0, 0 ) != 0 ) {
            throw std::exception();
        }
    }
    my_sem(int num) {               // 初始化信号量的值为num
        if( sem_init( &m_sem, 0, num ) != 0 ) {
            throw std::exception();
        }
    }
    ~my_sem() {                     // 释放信号量
        sem_destroy( &m_sem );
    }
    bool wait() {                   // 等待信号量
        return sem_wait( &m_sem ) == 0;
    }
    bool post() {                   // 增加信号量
        return sem_post( &m_sem ) == 0;
    }
private:
    sem_t m_sem;                    // 信号量
};

#endif