#pragma once
#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <sys/sendfile.h> 
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>      

#include "log.h"
#include "sqlconnRAII.h"
#include "buffer.h"
#include "httprequest.h"
#include "httpresponse.h"
#include "webdisk.h"

class HttpConn {
public:
    HttpConn();

    ~HttpConn();

    void init(int sockFd, const sockaddr_in& addr);

    ssize_t read(int* saveErrno);

    ssize_t write(int* saveErrno);

    void Close();

    int GetFd() const;

    int GetPort() const;

    const char* GetIP() const;
    
    sockaddr_in GetAddr() const;
    
    bool process();

    int ToWriteBytes();

    bool IsKeepAlive() const;

    static bool isET;
    static const char* srcDir;
    static std::atomic<int> userCount;
    
private:
   
    int fd_;
    struct  sockaddr_in addr_;

    std::atomic<bool> isClose_;

    off_t fileOffset_; // 新增：文件发送偏移量
    
    int iovCnt_;
    struct iovec iov_[2];
    
    Buffer readBuff_; // 读缓冲区
    Buffer writeBuff_; // 写缓冲区

    std::shared_ptr<HttpRequest> request_;
    std::shared_ptr<HttpResponse> response_;

};
