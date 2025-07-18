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

#include "sqlconnRAII.h"
#include "httprequest.h"
#include "httpresponse.h"

namespace fs = std::filesystem;

class WebDisk {
public:
    WebDisk();
    void Init(const HttpRequest& req, HttpResponse& res);
    void Handle();
private:
    bool Login(); // 用户登录
    bool Register(); // 用户登录
    bool ListFiles(); // 文件列表
    bool UploadFile(); // 分块上传
    bool DownloadFile(); // 零拷贝下载
    bool DeleteFile(); // 文件删除
private:
    std::string GenerateSessionToken();
    std::string ParseSessionToken();
    std::string SanitizePath(const std::string& path);
    void MergeChunks(const std::string& output_path, const std::string& tmp_dir, const std::string& base_name);
    bool UserExists(const std::string& user);
    std::string GetUserDir();
private:
    HttpRequest request_;
    HttpResponse response_;
    std::string username_;
    std::string password_;

    std::string rootDir_;   // 存储根目录
    std::string userDir_;        // 当前用户目录
    std::unordered_map<std::string, std::string> sessions_; // 会话表
    std::mutex mtx_;    // 会话操作锁

    static const std::unordered_map<std::string, int> FUNC_TAG;

};