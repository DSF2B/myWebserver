#include "webserver.h"

WebServer::WebServer(int port, int trigMode, int timeoutMS, bool OptLinger, 
int sqlPort, const char* sqlUser, const  char* sqlPwd, 
const char* dbName, int connPoolNum, int threadNum,
bool openLog, int logLevel, int logQueSize)
{
    //设置服务器参数　＋　初始化定时器／线程池／反应堆／连接队列
    int port_;
    bool openLinger_;
    int timeoutMS_;  /* 毫秒MS */
    bool isClose_;
    int listenFd_;
    char* srcDir_;
    
    uint32_t listenEvent_;
    uint32_t connEvent_;
   
    std::unique_ptr<HeapTimer> timer_;
    std::unique_ptr<ThreadPool> threadpool_;
    std::unique_ptr<Epoller> epoller_;
    std::unordered_map<int, HttpConn> users_;     
}

WebServer::~WebServer(){
    //关闭listenFd_，　销毁　连接队列/定时器／线程池／反应堆

}


bool WebServer::InitSocket_(){
    
}
void WebServer::InitEventMode_(int trigMode){
    
}
void WebServer::Start(){
    
}


void WebServer::DealListen_(){
    
}
void WebServer::AddClient_(int fd, sockaddr_in addr){
    
}
void WebServer::SendError_(int fd, const char*info){
    
}


void WebServer::DealWrite_(HttpConn* client){
    
}
void WebServer::DealRead_(HttpConn* client){
    
}
void WebServer::ExtentTime_(HttpConn* client){
    
}



void WebServer::OnRead_(HttpConn* client){
    
}
void WebServer::OnWrite_(HttpConn* client){
    
}
void WebServer::OnProcess(HttpConn* client){
    
}


void WebServer::CloseConn_(HttpConn* client){
    
}
int WebServer::SetFdNonblock(int fd){
    assert(fd>0);
    int old_option=fcntl(fd,F_GETFD);
    return fcntl(fd,F_SETFD,old_option|O_NONBLOCK);
}

