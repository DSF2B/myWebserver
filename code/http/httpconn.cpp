#include "httpconn.h"

bool HttpConn::isET;
const char* HttpConn::srcDir;
std::atomic<int> HttpConn::userCount; 

HttpConn::HttpConn(){
    fd_=-1;
    addr_={0};
    isClose_=true;
    fileOffset_ = 0; // 新增：文件发送偏移量
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

    request_ = std::make_shared<HttpRequest>();
    response_ = std::make_shared<HttpResponse>();

    LOG_INFO("Client[%d](%s:%d) in, userCount:%d",fd_,GetIP(),GetPort(),(int)userCount);
}
void HttpConn::Close(){
    // response_.UnmapFile();
    if(isClose_==false){
        isClose_=true;
        userCount--;
        response_->CloseFd();
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

// ssize_t HttpConn::write(int* saveErrno){
//     ssize_t len=-1;
//     do{
//         len=writev(fd_,iov_,iovCnt_);
//         if(len<=0){
//             *saveErrno = errno;
//             break;
//         }
//         if(iov_[0].iov_len + iov_[1].iov_len == 0){
//             break;
//         }else if(static_cast<size_t>(len) > iov_[0].iov_len){
//             //写入长度超过第一个缓冲区
//             iov_[1].iov_base=(uint8_t*)iov_[1].iov_base + (len-iov_[0].iov_len);
//             iov_[1].iov_len -= (len - iov_[0].iov_len);
//             if(iov_[0].iov_len){
//                 //响应头已经发送完毕，释放writeBuff_
//                 writeBuff_.RetrieveAll();
//                 iov_[0].iov_len=0;
//             }
//         }else{
//             //写入长度未超第一个缓冲区
//             iov_[0].iov_base=(uint8_t*)iov_[0].iov_base + len;
//             iov_[0].iov_len-=len;
//             writeBuff_.Retrieve(len);
//         }
//     }while(isET || ToWriteBytes() > 10240);
//     return len;
// }
ssize_t HttpConn::write(int* saveErrno){
    ssize_t totalToSent= static_cast<int>(iov_[0].iov_len+iov_[1].iov_len+response_->FileLen() - fileOffset_);
    if(iov_[0].iov_len+iov_[1].iov_len>0){
        do{
            ssize_t len=writev(fd_,iov_,iovCnt_);
            if(len==0){
                break;
            }
            else if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                } else {
                    *saveErrno = errno;
                    return -1;
                }
            }
            totalToSent-=len;
            if(static_cast<size_t>(len) > iov_[0].iov_len){
                //响应头已经发送完毕，释放writeBuff_
                iov_[1].iov_base=(uint8_t*)iov_[1].iov_base + (len-iov_[0].iov_len);
                iov_[1].iov_len -= (len - iov_[0].iov_len);
                if(iov_[0].iov_len){
                    writeBuff_.RetrieveAll();
                    iov_[0].iov_len=0;
                }
                if(iov_[1].iov_len==0){
                    iov_[1].iov_base=nullptr;
                    break;
                }
            }
            else if(static_cast<size_t>(len) < iov_[0].iov_len){
                //写入长度未超第一个缓冲区
                iov_[0].iov_base=(uint8_t*)iov_[0].iov_base + len;
                iov_[0].iov_len-=len;
                writeBuff_.Retrieve(len);
            }else{
                writeBuff_.RetrieveAll();
                iov_[0].iov_len=0;
                iov_[0].iov_base=nullptr;
                if(iovCnt_==1){
                    break;
                }

            }
        }while(isET || iov_[0].iov_len+iov_[1].iov_len > 10240);
    }
    if(iov_[0].iov_len+iov_[1].iov_len==0 && totalToSent > 0 && response_->File()!=-1){
        //前一个已经发完才能才第二个
        do{
            ssize_t sent = sendfile(fd_, response_->File(), &fileOffset_, response_->FileLen() - fileOffset_);
            if(sent==0){
                break;
            }
            else if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // 可写资源耗尽
                } else {
                    *saveErrno = errno;
                    return -1;
                }
            }else{
                totalToSent-=sent;
                if(totalToSent==0)break;
            }
            
        }while((isET || totalToSent > 10240));
    }
    if(totalToSent>0)*saveErrno = EAGAIN;

    if(totalToSent>0)return -1;
    else return 0;
}
bool HttpConn::process(){
    
    request_->Init();
    if(readBuff_.ReadableBytes()<=0){
        return false;
    }
    else if(request_->parse(readBuff_)){
        LOG_DEBUG("%s",request_->path().c_str());
    }
    webdisk_.Init(request_,response_,srcDir);
    webdisk_.Handle();

    writeBuff_.RetrieveAll();
    response_->MakeResponse(writeBuff_);
    iov_[0].iov_base=const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len=writeBuff_.ReadableBytes();
    iovCnt_=1;
    
    if(response_->sendFileType() == SendFileType::DynamicWebPage){
        iov_[1].iov_base=&response_->body()[0];
        iov_[1].iov_len=response_->body().size();
        iovCnt_=2;
    }
    else{
        //sendfile(fileFd_)
    }
    fileOffset_=0;
    // if(response_.FileLen()>0 && response_.File()){ 
    //     //若有响应体
    //     iov_[1].iov_base=response_.File();
    //     iov_[1].iov_len=response_.FileLen();
    //     iovCnt_=2;
    // }
    LOG_DEBUG("filesize:%d, %d  to %d", response_->FileLen() , iovCnt_, ToWriteBytes());
    return true;
}

int HttpConn::ToWriteBytes() { 
    // return iov_[0].iov_len + iov_[1].iov_len; 
    return static_cast<int>(iov_[0].iov_len+iov_[1].iov_len +response_->FileLen() - fileOffset_);
}

bool HttpConn::IsKeepAlive() const {
    return request_->IsKeepAlive();
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