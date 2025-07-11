#include "httpconn.h"

bool HttpConn::isET;
const char* HttpConn::srcDir;
std::atomic<int> HttpConn::userCount;

HttpConn::HttpConn(){
    fd_=-1;
    addr_={0};
    isClose_=true;
}

HttpConn::~HttpConn(){
    Close();
}

void HttpConn::init(int sockFd, const sockaddr_in& addr){
    assert(sockFd>0);
    userCount++;
    addr_=addr;
    fd_=sockFd;

    readBuff_.RetrieveAll();
    writeBuff_.RetrieveAll();
    isClose_=false;

    LOG_INFO("Client[%d](%s:%d) in, userCount:%d",fd_,GetIP(),GetPort(),(int)userCount);
}
void HttpConn::Close(){
    response_.UnmapFile();
    if(isClose_==false){
        isClose_==true;
        userCount--;
        close(fd_);
        LOG_DEBUG("Client[%d](%s:%d) quit, userCount:%d",fd_,GetIP(),GetPort(),(int)userCount);
    }
}
ssize_t HttpConn::read(int* saveErrno){
    ssize_t len;
    do{
        len=readBuff_.ReadFd(fd_,saveErrno);
        if(len<=0){
            break;
        }
    }while(isET);
    return len;
}
// ssize_t HttpConn::read(int* saveErrno) {
//     ssize_t total_len = 0;  // 累积读取的总字节数
//     ssize_t len = 0;        // 单次读取的字节数

//     do {
//         len = readBuff_.ReadFd(fd_, saveErrno);
        
//         // 处理读取结果
//         if (len < 0) {
//             if (errno == EAGAIN || errno == EWOULDBLOCK) {
//                 break;  // ET模式：内核缓冲区空，正常退出
//             } else {
//                 *saveErrno = errno; // 记录非临时性错误
//                 return -1;
//             }
//         } else if (len == 0) {
//             *saveErrno = ECONNRESET; // 对端关闭连接
//             return -1;
//         }
        
//         total_len += len; // 累积有效数据长度
//     } while (isET);  // ET模式循环读取，LT模式仅读一次

//     return total_len; // 返回实际读取的总字节数
// }
ssize_t HttpConn::write(int* saveErrno){
    ssize_t len=-1;
    do{
        len=writev(fd_,iov_,iovCnt_);
        if(len<=0){
            *saveErrno = errno;
            break;
        }
        if(iov_[0].iov_len + iov_[1].iov_len == 0){
            break;
        }else if(static_cast<size_t>(len) > iov_[0].iov_len){
            //写入长度超过第一个缓冲区
            iov_[1].iov_base=(uint8_t*)iov_[1].iov_base + (len-iov_[0].iov_len);
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            if(iov_[0].iov_len){
                //响应头已经发送完毕，释放writeBuff_
                writeBuff_.RetrieveAll();
                iov_[0].iov_len=0;
            }
        }else{
            //写入长度未超第一个缓冲区
            iov_[0].iov_base=(uint8_t*)iov_[0].iov_base + len;
            iov_[0].iov_len-=len;
            writeBuff_.Retrieve(len);
        }
    }while(isET || ToWriteBytes() > 10240);
    return len;
}

bool HttpConn::process(){
    request_.Init();
    if(readBuff_.ReadableBytes()<=0){
        return false;
    }
    else if(request_.parse(readBuff_)){
        LOG_DEBUG("%s",request_.path().c_str());
        response_.Init(srcDir,request_.path(),request_.IsKeepAlive(),200);
    }else{
        response_.Init(srcDir,request_.path(),false,400);
    }

    response_.MakeResponse(writeBuff_);
    iov_[0].iov_base=const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len=writeBuff_.ReadableBytes();
    iovCnt_=1;

    if(response_.FileLen()>0 && response_.File()){
        //若有响应体
        iov_[1].iov_base=response_.File();
        iov_[1].iov_len=response_.FileLen();
        iovCnt_=2;
    }
    LOG_DEBUG("filesize:%d, %d  to %d", response_.FileLen() , iovCnt_, ToWriteBytes());
    return true;
}

int HttpConn::ToWriteBytes() { 
    return iov_[0].iov_len + iov_[1].iov_len; 
}

bool HttpConn::IsKeepAlive() const {
    return request_.IsKeepAlive();
}


int HttpConn::GetFd() const{
    return fd_;
}

int HttpConn::GetPort() const{
    return addr_.sin_port;
}

const char* HttpConn::GetIP() const{
    return inet_ntoa(addr_.sin_addr);
}

sockaddr_in HttpConn::GetAddr() const{
    return addr_;
}