#pragma once
#include <unordered_map>
#include <fcntl.h>       // fcntl()
#include <unistd.h>      // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
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
    void AddClient(int fd, uint32_t event,sockaddr_in addr);

private:
    void Stop();                   // 停止事件循环
    static int SetFdNonblock(int fd);
    void Loop();

    void ThreadPoolDealRead_(HttpConn* client);
    void ThreadPoolDealWrite_(HttpConn* client);
    void OnRead_(HttpConn* client);
    void OnWrite_(HttpConn* client);
    void ExtentTime_(HttpConn* client);
    void CloseConn_(HttpConn* client);
    void OnProcess(HttpConn* client);

private:
    int connEvent_;
    int timeoutMS_;
    bool isRunning_;
    bool openThreadPool_;

    std::unique_ptr<Epoller> epoller_;
    std::shared_ptr<ThreadPool> threadpool_;
    std::unique_ptr<HeapTimer> timer_;
    std::unordered_map<int, HttpConn> users_;
    std::thread thread_;
    
};