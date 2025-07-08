#include "webserver.h"

WebServer::WebServer(int port, int trigMode, int timeoutMS, bool OptLinger, 
    int sqlPort, const char* sqlUser, const  char* sqlPwd, 
    const char* dbName, int connPoolNum, int threadNum,
    bool openLog, int logLevel, int logQueSize):
    port_(port),openLinger_(OptLinger),timeoutMS_(timeoutMS),isClose_(false),
    timer_(new HeapTimer()),threadpool_(new ThreadPool(threadNum)),epoller_(new Epoller())
{
    //设置服务器参数　＋　初始化定时器／线程池／反应堆／连接队列
    srcDir_=getcwd(nullptr,256);
    assert(srcDir_);
    std::strcat(srcDir_,"resources");
    HttpConn::userCount=0;
    HttpConn::srcDir=srcDir_;

    SqlConnPool::Instance()->Init("localhost",sqlPort,sqlUser,sqlPwd,dbName,connPoolNum);
    InitEventMode_(trigMode);
    if(!InitSocket_()){
        isClose_=true;
    }
    if(openLog){
        Log::Instance()->init(logLevel,"./log",".log",logQueSize);
        if(isClose_){
            LOG_ERROR("========== Server init error!==========");
        }        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }        
    }   
}

WebServer::~WebServer(){
    //关闭listenFd_，　销毁　连接队列/定时器／线程池／反应堆
    close(listenFd_);
    isClose_=true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

void WebServer::InitEventMode_(int trigMode){
    listenEvent_=EPOLLRDHUP;
    connEvent_=EPOLLONESHOT | EPOLLRDHUP;
    switch(trigMode){
        case 0:
        {   
            break;
        }
        case 1:
        {
            connEvent_ |=EPOLLET;
            break;
        }
        case 2:
        {
            listenEvent_|=EPOLLET;
            break;
        }
        case 3:
        {
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
        }
        default:
        {
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
        }
    }
    HttpConn::isET=(connEvent_ & EPOLLET);
}
bool WebServer::InitSocket_(){
    int ret;
    struct sockaddr_in addr;
    if(port_ > 65535 || port_< 1024){
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    addr.sin_port=htons(port_);
    //网络传输数据采用的是大端顺序,编译器里面设置的是小端顺序
    //htonl:将一个32位数从主机字节顺序转换成网络字节顺序
    //htons:将一个16位数从主机字节顺序转换成网络字节顺序
    //ntohs():将一个16位数由网络字节顺序转换为主机字节顺序
    //ntohl():将一个32位数由网络字节顺序转换为主机字节顺序

    {
        //struct linger用于控制调用 close() 关闭套接字时的行为
        struct linger optLinger={0};
        if(openLinger_){
            optLinger.l_onoff = 1;        // 启用 SO_LINGER
            optLinger.l_linger = 1;       // 设置超时为 1 秒
        }
    }

    listenFd_=socket(AF_INET,SOCK_STREAM,0);
    if(listenFd_<0){
        LOG_ERROR("Create socket error!", port_);
        return false;
    }
    int optval=1;
    ret=setsockopt(listenFd_,SOL_SOCKET,SO_REUSEADDR,(const void*)&optval,sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }
    ret=bind(listenFd_,(struct sockaddr*)&addr,sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    ret=listen(listenFd_,6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    ret=epoller_->AddFd(listenFd_,listenEvent_ | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;


}

void WebServer::Start(){
    int timeMS=-1;// epoll wait timeout == -1 无事件将阻塞
    if(!isClose_){
        LOG_INFO("========== Server start =========="); 
    }
    while(!isClose_){
        if(timeoutMS_ > 0){
            timeMS=timer_->GetNextTick();
        }
        int eventCnt=epoller_->Wait(timeMS);
        for(int i=0;i<eventCnt;i++){
            int fd=epoller_->GetEventFd(i);
            uint32_t events=epoller_->GetEvents(i);
            if(fd==listenFd_){
                DealListen_();
            }else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                auto it=users_.find(fd);
                assert(it != users_.end());
                CloseConn_(&it->second);
            }else if(events & EPOLLIN){
                auto it=users_.find(fd);
                assert(it != users_.end());
                DealRead_(&it->second); 
            }else if(events & EPOLLOUT){
                auto it=users_.find(fd);
                assert(it != users_.end());
                DealWrite_(&it->second);
            }else{
                LOG_ERROR("Unexpected event");
            }

        }
    }
}

void WebServer::DealListen_(){
    struct sockaddr_in addr;
    socklen_t len=sizeof(addr);
    do{
        int fd=accept(fd,(struct sockaddr*)&addr,&len);
        if(fd<=0)return ;
        else if(HttpConn::userCount >=MAX_FD){
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        AddClient_(fd,addr);
    }while(listenEvent_ & EPOLLET);
}
void WebServer::AddClient_(int fd, sockaddr_in addr){
    assert(fd>0);
    users_[fd].init(fd,addr);
    if(timeoutMS_ >0){
        timer_->add(fd,timeoutMS_,std::bind(&WebServer::CloseConn_,this,&users_[fd]));
    }
    epoller_->AddFd(fd,EPOLLIN | connEvent_);
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}
void WebServer::CloseConn_(HttpConn* client){
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
}
void WebServer::SendError_(int fd, const char*info){
    assert(fd>0);
    int ret=send(fd,info,strlen(info),0);
    if(ret<0){
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}


void WebServer::DealRead_(HttpConn* client){
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnRead_,this,client));
}
void WebServer::ExtentTime_(HttpConn* client){
    assert(client);
    if(timeoutMS_>0){
        timer_->adjust(client->GetFd(),timeoutMS_);
    }
}
void WebServer::DealWrite_(HttpConn* client){
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_,this,client));
}

void WebServer::OnRead_(HttpConn* client){
    assert(client);
    int ret=-1;
    int readErrno=0;
    ret=client->read(&readErrno);
    if(ret<=0 && readErrno!=EAGAIN){
        CloseConn_(client);
        return;
    }
    OnProcess(client);
}
void WebServer::OnWrite_(HttpConn* client){
    assert(client);
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
void WebServer::OnProcess(HttpConn* client){
    if(client->process()){
        epoller_->ModFd(client->GetFd(),connEvent_ | EPOLLOUT);
    }else{
        epoller_->ModFd(client->GetFd(),connEvent_ | EPOLLIN);
    }
}

int WebServer::SetFdNonblock(int fd){
    assert(fd>0);
    int old_option=fcntl(fd,F_GETFD);
    return fcntl(fd,F_SETFD,old_option|O_NONBLOCK);
}

