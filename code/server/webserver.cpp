#include "webserver.h"

WebServer::WebServer(int port, int trigMode, int timeoutMS, bool OptLinger,  
    int sqlPort, const char* sqlUser, const  char* sqlPwd, 
    const char* dbName, int connPoolNum, int threadNum,
    bool openLog, int logLevel, int logQueSize,bool openThreadPool):
    port_(port),openLinger_(OptLinger),isClose_(false),
    epoller_(new Epoller())
{
    //设置服务器参数　＋　初始化定时器／线程池／反应堆／连接队列
    srcDir_=getcwd(nullptr,256);
    assert(srcDir_);
    std::strncat(srcDir_, "/resources", 16);
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
        }        
        else {
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
    if(openThreadPool){
        threadpool_=std::make_shared<ThreadPool>(threadNum);
    }else{
        threadpool_=nullptr;
    }
    
    for (int i = 0; i < subReactorNum_; ++i) {
        subReactors_.emplace_back(std::make_unique<SubReactor>(timeoutMS,connEvent_,openThreadPool,threadpool_));
        subReactors_[i]->Start(); // 启动子线程
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

    //struct linger用于控制调用 close() 关闭套接字时的行为
    struct linger optLinger={0};
    if(openLinger_){
        optLinger.l_onoff = 1;        // 启用 SO_LINGER
        optLinger.l_linger = 1;       // 设置超时为 1 秒
    }

    listenFd_=socket(AF_INET,SOCK_STREAM,0);
    if(listenFd_<0){
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
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

    ret=listen(listenFd_,1024);
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

    // 设置接收缓冲区大小
    int rcvbuf = 256 * 1024; // 256KB
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    if(ret < 0) {
        LOG_WARN("Set SO_RCVBUF failed");
    }
    
    // 设置发送缓冲区大小  
    int sndbuf = 256 * 1024; // 256KB
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    if(ret < 0) {
        LOG_WARN("Set SO_SNDBUF failed");
    }
    return true;
}

void WebServer::Start() {
    if(!isClose_) { LOG_INFO("========== Server start =========="); }
    while(!isClose_) {
        int eventCnt = epoller_->Wait(100);
        for(int i = 0; i < eventCnt; i++) {
        //     /* 处理事件 */
            int fd = epoller_->GetEventFd(i);
            if(fd == listenFd_) {
                DealListen_();
            }
        }
    }
}  

void WebServer::DealListen_(){
    struct sockaddr_in addr;
    socklen_t len=sizeof(addr);
    do{
        int fd=accept(listenFd_,(struct sockaddr*)&addr, &len);
        if (fd <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 无更多连接
            else return; // 其他错误
        }
        // std::cout<<"acceptfd:"<<fd<<std::endl;
        AddClient_(fd,addr);
        if(HttpConn::userCount >=MAX_FD){
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
    }while(listenEvent_ & EPOLLET);
}
void WebServer::AddClient_(int fd, sockaddr_in addr){
    assert(fd>0);
    // 轮询选择一个子Reactor
    int subReactorIdx = fd % subReactors_.size();
    // int subReactorIdx = rand() % subReactors_.size();  // 使用随机数生成器
    auto& subReactor = subReactors_[subReactorIdx];
    // subReactor->AddClient(fd, EPOLLIN | connEvent_, addr);//修改subreactor，需要锁
    if (!subReactor->PushClient(fd, addr)) {
        LOG_WARN("SubReactor[%d] queue full! Dropping fd=%d", subReactorIdx, fd);
        close(fd);  // 队列满时立即关闭连接
        return;
    }
}

void WebServer::SendError_(int fd, const char*info){
    assert(fd>0);
    int ret=send(fd,info,strlen(info),0);
    if(ret<0){
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}
int WebServer::SetFdNonblock(int fd){
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


