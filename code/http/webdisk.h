#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <mutex>
#include <sys/sendfile.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <sstream>
#include <cctype>
#include <random>

#include "sqlconnRAII.h"
#include "httprequest.h"
#include "httpresponse.h"
#include "sessionmanager.h"

namespace fs = std::filesystem;
class WebDisk {
public:
    WebDisk();
    void Init(std::shared_ptr<HttpRequest> req, std::shared_ptr<HttpResponse> res,const std::string srcDir);    
    void Handle();
private:
    void Login(); // 用户登录
    void Register(); // 用户登录
    void ListFiles(); // 文件列表
    void UploadFile(); // 分块上传
    void DownloadFile(); // 零拷贝下载 
    void DeleteFile(); // 文件删除
    void LogOut();
private:
    void ParsePost();
    void ParseFromUrlencoded();// 从url中解析编码
    void ParseFromData();

    void ParsePath();
    void ParseFromDownloadPath();
    void ParseFromDeletePath();

    
    static int ConverHex(char ch);

    std::string GenerateSessionToken();
    std::string ParseSessionToken();
    std::string SanitizePath(const std::string& path);
    void MergeChunks(const std::string& file_id, const std::string& file_name);
    bool UserExists(const std::string& user);
    std::string GetUserDir();
    std::string GenerateFileUUID();
    
private:
    std::shared_ptr<HttpRequest> request_;
    std::shared_ptr<HttpResponse> response_;
    std::string username_;
    std::string password_;
    std::string srcDir_;//资源目录

    std::string path_;
    std::unordered_map<std::string, std::string> post_;

    std::string rootDir_;   // 用户存储根目录
    std::string userDir_;        // 当前用户目录
    std::string filePath_;  //用户操作的文件目录
    std::mutex mtx_;    

    static const std::unordered_map<std::string, int> FUNC_TAG;
    static const std::unordered_set<std::string> DEFAULT_HTML;

};