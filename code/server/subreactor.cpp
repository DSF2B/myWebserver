#include "subreactor.h"

SubReactor::SubReactor(int timeoutMS,int connEvent,bool openThreadPool,std::shared_ptr<ThreadPool> threadpool):
connEvent_(connEvent),
timeoutMS_(timeoutMS),
isRunning_(false),
openThreadPool_(openThreadPool),
timer_(std::make_unique<HeapTimer>()),
epoller_(std::make_unique<Epoller>()),
threadpool_(threadpool)
{}

SubReactor::~SubReactor(){
    Stop();
}

void SubReactor::Start() {
    isRunning_ = true;
    thread_ = std::thread([this] { Loop(); });
}
void SubReactor::Stop() {
    isRunning_ = false;
    if (thread_.joinable()) thread_.join();
}
void SubReactor::Loop() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    while (isRunning_) {
        if(timeoutMS_ > 0) {
            timeMS = timer_->GetNextTick();
        }
        int eventCnt = epoller_->Wait(timeMS);
        for (int i = 0; i < eventCnt; ++i) {
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);

            auto it = users_.find(fd);
            if (it == users_.end()) continue; // 连接已关闭
            if (events & EPOLLIN) {
                if (openThreadPool_) {
                    ThreadPoolDealRead_(&it->second);  // 线程池处理业务
                }else{
                    OnRead_(&it->second);
                }
            }
            else if(events & EPOLLOUT){
                OnWrite_(&it->second);
            } 
            else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                CloseConn_(&it->second);  // 连接关闭事件直接处理
            }
        }
    }
}

void SubReactor::AddClient(int fd, uint32_t event,sockaddr_in addr)  {
    users_[fd].init(fd,addr);
    if(timeoutMS_ >0){
        timer_->add(fd,timeoutMS_,std::bind(&SubReactor::CloseConn_,this, &users_[fd]));
    }
    SetFdNonblock(fd);
    epoller_->AddFd(fd, event); // ET边缘触发模式
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

void SubReactor::ThreadPoolDealRead_(HttpConn* client){
    threadpool_->AddTask(std::bind(&SubReactor::OnRead_,this, client));
}

void SubReactor::ThreadPoolDealWrite_(HttpConn* client){
    threadpool_->AddTask(std::bind(&SubReactor::OnWrite_,this,client));
}

void SubReactor::OnRead_(HttpConn* client){
    assert(client);
    ExtentTime_(client);
    int ret=-1;
    int readErrno=0;
    ret=client->read(&readErrno);
    if(ret<=0 && readErrno!=EAGAIN){
        CloseConn_(client);
        return;
    }
    OnProcess(client);
}
void SubReactor::OnWrite_(HttpConn* client){
    assert(client);
    ExtentTime_(client);
    int ret=-1;
    int writeErrno=0;
    ret=client->write(&writeErrno);
    if(client->ToWriteBytes() == 0){
        if(client->IsKeepAlive()){
            epoller_->ModFd(client->GetFd(), EPOLLIN | connEvent_);
            return ;
        }
    }else if(ret <0){
        if(writeErrno == EAGAIN){
            //内核发送缓冲区已满
            epoller_->ModFd(client->GetFd(),connEvent_ | EPOLLOUT);
            return ;
        }
    }
    //数据未写完且错误码非EAGAIN时
    CloseConn_(client);
}
void SubReactor::ExtentTime_(HttpConn* client){
    assert(client);
    if(timeoutMS_>0){
        timer_->adjust(client->GetFd(),timeoutMS_);
    }
}
void SubReactor::CloseConn_(HttpConn* client){
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
}
void SubReactor::OnProcess(HttpConn* client){
    if(client->process()){
        epoller_->ModFd(client->GetFd(),connEvent_ | EPOLLOUT);
    }else{
        epoller_->ModFd(client->GetFd(),connEvent_ | EPOLLIN);
    }
}

int SubReactor::SetFdNonblock(int fd){
    assert(fd>0);
    // 复制一个已经有的描述符（cmd=F_DUPFD或者F_DUPFD_CLOEXEC）
    // 获取/设置文件描述符标志（cmd=F_GETFD或者F_SETFD）
    // 获取/设置文件状态标志（cmd=F_GETFL或者F_SETFL）
    int old_option=fcntl(fd,F_GETFD);
    //F_GETFD\F_SETFD:FD_CLOEXEC
    //F_GETFL:O_RDONLY 、O_WRONLY、O_RDWR
    //F_SETFL:O_APPEND、 O_ASYNC、 O_DIRECT、 O_NOATIME、O_NONBLOCK
    return fcntl(fd,F_SETFL,old_option|O_NONBLOCK);
}
