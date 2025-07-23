#include "subreactor.h"

SubReactor::SubReactor(int timeoutMS,int connEvent,bool openThreadPool,std::shared_ptr<ThreadPool> threadpool):
connEvent_(connEvent),
timeoutMS_(timeoutMS),
isRunning_(false), 
openThreadPool_(openThreadPool),
timer_(std::make_unique<HeapTimer>()),
epoller_(std::make_unique<Epoller>()),
threadpool_(threadpool)
{
    conn_queue_.resize(queue_capacity_);
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
    // 创建eventfd并设置为非阻塞
    notify_fd_ = eventfd(0, EFD_NONBLOCK);
    SetFdNonblock(notify_fd_); // 复用已有的SetFdNonblock方法
    // 注册event_fd到epoller_，监听读事件（边缘触发）
    epoller_->AddFd(notify_fd_, EPOLLIN | EPOLLET | EPOLLONESHOT);
}

SubReactor::~SubReactor(){
    close(notify_fd_); // 新增
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
    int timeMS = -1;
    while (isRunning_) {
        if(timeoutMS_ > 0){
            timeMS = timer_->GetNextTick();
            // 限制最小超时时间，提高响应性
            if(timeMS > 1000) timeMS = 1000;
        } else {
            timeMS = 100; // 没有定时器时使用100ms超时
        }
        
        int eventCnt = epoller_->Wait(timeMS);
        
        for (int i = 0; i < eventCnt; ++i) {
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            if (fd == notify_fd_) { 
                if (events & EPOLLIN) {
                    //conn_queue积累够一定数量再通知epoll
                    // 读取eventfd并处理队列
                    HandleQueueNotification();
                }else if (events & (EPOLLRDHUP | EPOLLERR)) { // 处理eventfd异常
                    close(notify_fd_);
                    notify_fd_ = eventfd(0, EFD_NONBLOCK);
                    epoller_->ModFd(notify_fd_, EPOLLIN | EPOLLET | EPOLLONESHOT);
                }
            }
            else{
                auto it = users_.find(fd);
                if (it == users_.end()) continue; // 连接已关闭
                if (events & EPOLLIN) {
                    if (openThreadPool_) {
                        threadpool_->AddTask(std::bind(&SubReactor::OnRead_,this, &it->second));
                    }else{
                        OnRead_(&it->second);
                    }
                    // OnRead_(&it->second);
                }
                else if(events & EPOLLOUT){
                    if (openThreadPool_) {
                        threadpool_->AddTask(std::bind(&SubReactor::OnWrite_,this,&it->second));
                    }else{
                        OnWrite_(&it->second);
                    }
                }
                else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    CloseConn_(&it->second);  // 连接关闭事件直接处理
                }
            }
        }
        if(pending_fds_.size() >= queue_batch_size_) {
            // pending_fds_全部取出再添加
            ProcessPendingFds();
        }
    }
}

void SubReactor::HandleQueueNotification() {
    uint64_t value;
    read(notify_fd_, &value, sizeof(value)); // 清空eventfd
    epoller_->ModFd(notify_fd_, EPOLLIN | EPOLLET | EPOLLONESHOT);
    // 批量转移队列数据（减少CAS次数）
    size_t current_head = head_.load(std::memory_order_relaxed);
    // std::cout<<"conn_queue_ size:"<<conn_queue_.size()<<std::endl;
    while (current_head != tail_.load(std::memory_order_acquire)) {
        pending_fds_.push_back(conn_queue_[current_head]);
        current_head = (current_head + 1) % queue_capacity_;
    }
    batch_count_=0;
    head_.store(current_head, std::memory_order_release); // 原子更新头指针
    
}

void SubReactor::ProcessPendingFds() {
    for (auto it = pending_fds_.begin(); it != pending_fds_.end();) {
        int fd = *it;
        if (fcntl(fd, F_GETFD) == -1 && errno == EBADF) { // 检查 fd 有效性
            close(fd);
            it = pending_fds_.erase(it);
            continue;
        }

        sockaddr_in addr;
        socklen_t len = sizeof(addr);
        if (getpeername(fd, (sockaddr*)&addr, &len) == 0) {
            AddClient(fd, EPOLLIN | connEvent_ , addr);
        }  // 获取客户端地址
        ++it;
    }
    pending_fds_.clear();
}
void SubReactor::AddClient(int fd, uint32_t event,sockaddr_in addr)  {
    users_[fd].init(fd,addr);
    if(timeoutMS_ >0){
        timer_->add(fd,timeoutMS_,std::bind(&SubReactor::CloseConn_,this, &users_[fd]));
    }
      int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)); // 禁用 Nagle 算法
    
    SetFdNonblock(fd);
    epoller_->AddFd(fd, event); // ET边缘触发模式
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

bool SubReactor::PushClient(int fd, sockaddr_in addr){
    size_t current_tail = tail_.load(std::memory_order_acquire);
    size_t next_tail = (current_tail + 1) % queue_capacity_;
    // 队列满时直接拒绝（避免阻塞主Reactor）
    if (next_tail == head_.load(std::memory_order_acquire)) {
        return false; 
    }
    // 无锁写入fd
    conn_queue_[current_tail] = fd;
    tail_.store(next_tail, std::memory_order_release);
    // 写入eventfd通知SubReactor
    // 每积累queue_batch_size_个事件再通知
    batch_count_++;
    if (batch_count_ >= queue_batch_size_) {
        uint64_t value = 1;
        write(notify_fd_, &value, sizeof(value));
    }
    return true;
}

void SubReactor::OnRead_(HttpConn* client){
    assert(client);
    ExtentTime_(client);
    int ret=-1;
    int readErrno=0;
    ret=client->read(&readErrno);
    
    LOG_DEBUG("Client[%d] OnRead: ret=%d, errno=%d", client->GetFd(), ret, readErrno);
    
    if(ret > 0){
        if(client->IsRequestComplete()){
            OnProcess(client);
        }else{
            epoller_->ModFd(client->GetFd(), EPOLLIN | connEvent_);
        }
        // OnProcess(client);
    }
    else if(ret == 0){
        LOG_WARN("Client[%d] connection closed during chunk upload", client->GetFd());
        CloseConn_(client);
        return;
    }
    else if(readErrno == EAGAIN){
        // 正常，等待更多数据
        epoller_->ModFd(client->GetFd(), EPOLLIN | connEvent_);
        return;
    }
    else{
        LOG_ERROR("Client[%d] read error: %s", client->GetFd(), strerror(readErrno));
        CloseConn_(client);
        return;
    }
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
    users_.erase(client->GetFd());
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
    int old_option=fcntl(fd,F_GETFL);
    //F_GETFD\F_SETFD:FD_CLOEXEC
    //F_GETFL:O_RDONLY 、O_WRONLY、O_RDWR
    //F_SETFL:O_APPEND、 O_ASYNC、 O_DIRECT、 O_NOATIME、O_NONBLOCK
    return fcntl(fd,F_SETFL,old_option|O_NONBLOCK);
}
