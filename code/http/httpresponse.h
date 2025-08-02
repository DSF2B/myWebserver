#pragma once

#include <unordered_map>
#include <fcntl.h>       // open
#include <unistd.h>      // close
#include <sys/stat.h>    // stat
#include <sys/mman.h>    // mmap, munmap

#include "buffer.h"
#include "log.h"
enum class SendFileType {
    StaticWebPage,  // 静态网页
    DynamicWebPage, // 动态网页
    UserData        // 用户数据
};
class HttpResponse {
public:
    HttpResponse();
    ~HttpResponse(); 

    void Init(bool isKeepAlive = false, int code = -1);
    void MakeResponse(Buffer& buff);
    void UnmapFile();
    void CloseFd();
    char* mmFile();
    int File();
    size_t FileLen() const;
    void ErrorContent(Buffer& buff, std::string message);
    int Code() const;
    SendFileType sendFileType();
    std::string& body();
    void SetCode(const int& code);
    void SetHeader(const std::string& key, const std::string& value);
    void SetStaticFile(const std::string& path);
    void SetDynamicFile(const std::string& body);
    void SetBigFile(const std::string& userFilePath);
    int GetSocketFD() const;
private:
    void AddStateLine_(Buffer &buff);
    void AddHeader_(Buffer &buff);
    void AddContent_(Buffer &buff);

    void ErrorHtml_();
    std::string GetFileType_();

    int code_;
    bool isKeepAlive_;

    std::string body_;
    std::string path_;
    std::string userFilePath_;
    size_t userFileSize_;

    SendFileType sendFileType_;  // 替换 isSendUserData_

    std::unordered_map<std::string, std::string> headers_;

    char* mmFile_; 
    int fileFd_;
    
    struct stat mmFileStat_;

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;
    static const std::unordered_map<int, std::string> CODE_STATUS;
    static const std::unordered_map<int, std::string> CODE_PATH;
};
