#include "httpconn.h"


HttpConn::HttpConn(){
    static bool isET;
    static const char* srcDir;
    static std::atomic<int> userCount;
    int fd_;
    struct  sockaddr_in addr_;
    bool isClose_;
    int iovCnt_;
    struct iovec iov_[2];
    Buffer readBuff_; // 读缓冲区
    Buffer writeBuff_; // 写缓冲区
    HttpRequest request_;
    HttpResponse response_;
}

HttpConn::~HttpConn(){

}

void HttpConn::init(int sockFd, const sockaddr_in& addr){

}

ssize_t HttpConn::read(int* saveErrno){

}

ssize_t HttpConn::write(int* saveErrno){

}

void HttpConn::Close(){

}

int HttpConn::GetFd() const{

}

int HttpConn::GetPort() const{

}

const char* HttpConn::GetIP() const{

}

sockaddr_in HttpConn::GetAddr() const{

}

bool HttpConn::process(){
    
}

int HttpConn::ToWriteBytes() { 
    return iov_[0].iov_len + iov_[1].iov_len; 
}

bool HttpConn::IsKeepAlive() const {
    return request_.IsKeepAlive();
}
