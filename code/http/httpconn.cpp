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
    ssize_t total_len=0;
    do{
        len=readBuff_.ReadFd(fd_,saveErrno);
        if(len<=0){
            break;
        }else{
            total_len+=len;
        }
    }while(isET);
    return total_len>0?total_len:len;
}

ssize_t HttpConn::write(int* saveErrno){
    ssize_t totalToSent= static_cast<int>(iov_[0].iov_len+iov_[1].iov_len+response_->FileLen() - fileOffset_);
    if(totalToSent > iov_[0].iov_len+iov_[1].iov_len && response_->File() == -1){
        LOG_ERROR("Invalid file descriptor, cannot send file data");
        *saveErrno = EBADF;
        return -1;
    }
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

                writeBuff_.RetrieveAll();
                iov_[0].iov_len=0;
                iov_[0].iov_base=nullptr;

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
    if(totalToSent>0) *saveErrno = EAGAIN;
    response_->CloseFd();
    return (totalToSent > 0) ? -1 : 0;
}
bool HttpConn::process(){
    request_->Init();
    if(readBuff_.ReadableBytes()<=0){
        LOG_DEBUG("No readable data in buffer, returning false");
        return false;
    }
    if(request_->parse(readBuff_)){
        LOG_DEBUG("%s request body size:%d",request_->path().c_str(),request_->body().size());
        response_->Init(request_->IsKeepAlive(),-1);
        std::unique_ptr<WebDisk>webdisk = std::make_unique<WebDisk>(request_,response_,srcDir);
        webdisk->Handle();
    }else{
        LOG_ERROR("Request parse failed, readable bytes: %d", readBuff_.ReadableBytes());
        response_->Init(false,400);
        response_->SetDynamicFile("Bad Request - Please resend");
        return true;
    }
    readBuff_.RetrieveAll();
    writeBuff_.RetrieveAll();
    response_->MakeResponse(writeBuff_);
    iov_[0].iov_base=const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len=writeBuff_.ReadableBytes();
    iovCnt_=1;
    if(response_->sendFileType() == SendFileType::DynamicWebPage){
        iov_[1].iov_base=&response_->body()[0];
        iov_[1].iov_len=response_->body().size();
        iovCnt_=2;
    }else if(response_->sendFileType() == SendFileType::StaticWebPage){
        iov_[1].iov_base = response_->mmFile();
        iov_[1].iov_len = response_->FileLen();
        iovCnt_ = 2;
    }else{
        iov_[1].iov_base=nullptr;
        iov_[1].iov_len=0;
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

bool HttpConn::IsRequestComplete() const {
    // 简单检查：查找HTTP请求结束标志
    const char* data = readBuff_.Peek();
    size_t len = readBuff_.ReadableBytes();
    
    // 查找请求头结束标志
    const char* headerEnd = strstr(data, "\r\n\r\n");
    if (!headerEnd) {
        LOG_DEBUG("Headers incomplete");
        return false;
    }
    
    // 检查Content-Length
    std::string headers(data, headerEnd - data);
    size_t contentLengthPos = headers.find("Content-Length:");
    if (contentLengthPos != std::string::npos) {
        // 提取Content-Length值
        size_t valueStart = headers.find(':', contentLengthPos) + 1;
        size_t valueEnd = headers.find('\r', valueStart);
        std::string lengthStr = headers.substr(valueStart, valueEnd - valueStart);
        
        // 去除空格
        lengthStr.erase(0, lengthStr.find_first_not_of(" \t"));
        lengthStr.erase(lengthStr.find_last_not_of(" \t") + 1);
        
        try {
            size_t contentLength = std::stoul(lengthStr);
            size_t headerSize = (headerEnd - data) + 4; // +4 for "\r\n\r\n"
            size_t bodyReceived = len - headerSize;
            
            LOG_DEBUG("Content-Length: %zu, Body received: %zu/%zu", 
                     contentLength, bodyReceived, contentLength);
            
            return bodyReceived >= contentLength;
        } catch (const std::exception& e) {
            LOG_ERROR("Invalid Content-Length: %s", lengthStr.c_str());
            return false;
        }
    }
    
    // 没有Content-Length，认为请求完整
    return true;
}

size_t HttpConn::GetReadableBytes() const {
    return readBuff_.ReadableBytes();
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
