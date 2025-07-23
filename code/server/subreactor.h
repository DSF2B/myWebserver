#pragma once
#include <unordered_map>
#include <fcntl.h>       // fcntl()
#include <unistd.h>      // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>  // 定义 TCP_NODELAY
#include <arpa/inet.h>

#include "epoller.h"
#include "log.h"
#include "heaptimer.h"
#include "sqlconnpool.h"
#include "threadpool.h"
#include "httpconn.h"
 

class SubReactor{
public:
    SubReactor(int timeoutMS,int connEvent,bool openThreadPool, std::shared_ptr<ThreadPool>);
    ~SubReactor();
    void Start();                  // 启动子Reactor线程
    // void AddClient(int fd, uint32_t event,sockaddr_in addr);
    bool PushClient(int fd, sockaddr_in addr);
private:
    void Stop();                   // 停止事件循环
    static int SetFdNonblock(int fd);
    void Loop();

    void OnRead_(HttpConn* client);
    void OnWrite_(HttpConn* client);
    void ExtentTime_(HttpConn* client);
    void CloseConn_(HttpConn* client);
    void OnProcess(HttpConn* client);

    //lockqueue
    void HandleQueueNotification();
    void ProcessPendingFds();
    void AddClient(int fd, uint32_t event,sockaddr_in addr);

private:
    int connEvent_;
    int timeoutMS_;
    std::atomic<bool> isRunning_;
    bool openThreadPool_;
    std::shared_ptr<ThreadPool> threadpool_;

    std::unique_ptr<Epoller> epoller_;
    std::unique_ptr<HeapTimer> timer_;
    std::unordered_map<int, HttpConn> users_;

    std::thread thread_;
    // 无锁队列（环形缓冲区）
    alignas(64) std::atomic<size_t> head_{0};   // 缓存行对齐，避免伪共享
    alignas(64) std::atomic<size_t> tail_{0};
    std::vector<int> conn_queue_;                // 存储待注册的fd
    size_t queue_capacity_ = 1024;               // 队列容量（建议2的幂）
    // 事件通知通道
    int notify_fd_;                              // 通过eventfd创建
    // 批量处理缓冲区
    std::vector<int> pending_fds_;              // 暂存从队列取出的fd
    std::atomic<size_t> batch_count_{0};
    ssize_t queue_batch_size_=1;
};