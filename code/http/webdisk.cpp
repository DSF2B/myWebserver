#include "webdisk.h"
WebDisk::WebDisk(){
    rootDir_ = "./usrdata/webdisk_users";
    fs::create_directories(rootDir_);
    fs::create_directories(rootDir_ + "/tmp");
    fs::create_directories(rootDir_ + "/chunks");
}
const std::unordered_map<std::string, int> WebDisk::FUNC_TAG{
    {"/login",0},{"/register",1},{"/api/files",2},{"/upload",3},{"/download",4},{"/delete",5},{"/logout",6}
};
// // 文件列表
// GET /api/files
// // 文件上传
// POST /upload
// // 文件下载
// GET /download?file={filename}
// // 文件删除
// DELETE /delete?file={filename}
// // 退出登录
// POST /logout
const std::unordered_set<std::string> WebDisk::DEFAULT_HTML{
    "/index","/register","/login","/welcome","/video","/picture"
};
void WebDisk::Init(std::shared_ptr<HttpRequest> req, 
    std::shared_ptr<HttpResponse> res,
    const std::string srcDir){
    request_ = req;
    response_ = res;
    
    username_="";
    password_="";
    srcDir_=srcDir;
    post_.clear();
    userDir_="";
}
void WebDisk::Handle(){
    std::lock_guard<std::mutex> lock(mtx_);
    ParsePath();
    //path_:
    // "/index","/register","/login","/welcome","/video","/picture"
    // /api/files  /upload  /download  /delete  /logout

    auto it=FUNC_TAG.find(path_);
    if(it==FUNC_TAG.end()){
        //StaticWebpage
        return;
    }
    //解析请求体
    ParsePost();
    // 处理
    switch (it->second)
    {
    case 0:
        Login();
        break;
    case 1:
        Register();
        break;
    case 2:
        ListFiles();
        break;
    case 3:
        UploadFile();
        break;
    case 4:
        DownloadFile();
        break; 
    case 5:
        DeleteFile();
        break;
    case 6:
        LogOut();
    default:
        break;
    }
}
void WebDisk::ParsePath(){
    path_=request_->path();
    if(path_ == "/"){
        path_="/index";
        response_->SetStaticFile(srcDir_+"/index.html");
    }else if(path_ == "/api/files"){
        return;
    }else{
        auto it=DEFAULT_HTML.find(path_);
        if(it!=DEFAULT_HTML.end()){
            response_->SetStaticFile(srcDir_+it->data()+".html");
        }else{
            size_t query_pos = path_.find('?');
            if(query_pos !=std::string::npos){
                path_=path_.substr(0,query_pos);
                if(path_=="/download"){
                    ParseFromDownloadPath();
                }else if(path_=="/delete"){
                    ParseFromDeletePath();
                }
            }else{
                //css...
                response_->SetStaticFile(srcDir_+path_);
            }

        }
    }
}
void WebDisk::ParsePost(){
    if(request_->method() == "POST"){
        //POST
        std::string contentType = request_->GetHeader("Content-Type");
        size_t pos=contentType.find(";");
        std::string realType=contentType.substr(0,pos);
        if(realType == "application/x-www-form-urlencoded"){
            //Login,Register
            ParseFromUrlencoded();
        }else if(realType == "multipart/form-data"){
            //Upload
            ParseFromData();
        }
    }else if(request_->method() == "GET"){
        //GET
        //ListFiles
        //Download
        //WebPage
        //StaticWebpage
    }else if(request_->method() == "DELETE"){
        //DeleteFile
    }
}

void WebDisk::ParseFromUrlencoded(){
    //username=john%20doe&password=123
    //key=value&key=value
    //有特殊符号，将特殊符号转换为ASCII HEX值
    std::string body=request_->body();
    int n=body.size();
    if(n == 0)return;
    std::string key,value;
    int num=0;
    int i=0,j=0;
    for(;i<n;i++){
        char ch =body[i];
        switch(ch){
            case '=':{
                key=body.substr(j,i-j);
                j=i+1;
                break;
            }
            case '+':{
                body[i]=' ';
                break;
            }
            case '%':{
                num=ConverHex(body[i+1])*16 + ConverHex(body[i+2]);
                body[i+1]=num/10+'0';
                body[i+2]=num%10+'0';
                break;
            }
            case '&':{
                value = body.substr(j,i-j);
                j=i+1;
                post_[key]=value;
                LOG_DEBUG("%s = %s",key.c_str(),value.c_str());
                break;
            }
            default:
                break;
        }
    }
    assert(j<=i);
    if(post_.count(key) == 0 && j<i){
        //还没有存储value
        value=body.substr(j,i-j);
        post_[key]=value;
    }
    username_=post_["username"];
    password_=post_["password"];
}
void WebDisk::ParseFromData() {
    // 1. 验证会话令牌
    std::string token = ParseSessionToken();
    if (!SessionManager::getInstance().queryToken(token)) {
        response_->SetCode(401); // 未登录用户禁止操作
        return;
    }

    // 2. 设置用户专属目录 (格式: /rootDir/用户名)
    username_ = *SessionManager::getInstance().getSessionValue(token); // 从会话中提取用户名
    userDir_ = rootDir_ + "/" + SanitizePath(username_); // 安全过滤用户名[1](@ref)

    std::string contentType = request_->GetHeader("Content-Type");
    size_t boundaryPos = contentType.find("boundary=");
    if (boundaryPos == std::string::npos) {
        response_->SetCode(400); // 无效的Content-Type格式
        return;
    }
    // 1. 提取boundary
    std::string boundary = "--" + contentType.substr(boundaryPos + 9);
    std::string body = request_->body();
    size_t start = 0;
    while (start < body.size()) {
        // 2. 定位分片起始位置
        size_t partStart = body.find(boundary, start);
        if (partStart == std::string::npos) break;
        // 3. 解析分片头部
        size_t headerEnd = body.find("\r\n\r\n", partStart);
        if (headerEnd == std::string::npos) break;
        
        std::string headers = body.substr(
            partStart + boundary.size() + 2, 
            headerEnd - partStart - boundary.size() - 2
        );
        // 4. 解析字段名和文件名
        std::string name, filename;
        size_t namePos = headers.find("name=\"");
        if (namePos != std::string::npos) {
            size_t nameEnd = headers.find("\"", namePos + 6);
            name = headers.substr(namePos + 6, nameEnd - namePos - 6);
        }
        size_t filePos = headers.find("filename=\"");
        if (filePos != std::string::npos) {
            size_t fileEnd = headers.find("\"", filePos + 10);
            filename = headers.substr(filePos + 10, fileEnd - filePos - 10);
        }
        // 5. 提取分片内容
        size_t partEnd = body.find(boundary, headerEnd + 4);
        if (partEnd == std::string::npos) break;
        std::string content = body.substr(
            headerEnd + 4,
            partEnd - headerEnd - 6  // 减去末尾\r\n
        );
        // 6. 存储数据[3]
        if (!name.empty()) {
            if (!filename.empty()) {
                post_[name + "_filename"] = filename;       // 存储文件名
                post_[name + "_content"] = content;        // 存储文件内容
                LOG_DEBUG("File parsed: %s (Size: %zu bytes)", filename.c_str(), content.size());
            } else {
                // 普通字段：直接存储内容
                post_[name] = content;
            }
        }
        start = partEnd + boundary.size();
    }
}
void WebDisk::ParseFromDownloadPath(){
    // GET /download?file={filename}
    const std::string& path = request_->path();
    size_t query_pos = path.find('?');
    if (query_pos == std::string::npos) return;

    std::string query = path.substr(query_pos + 1);
    size_t file_param_pos = query.find("file=");
    if (file_param_pos == std::string::npos) return;

    size_t param_start = file_param_pos + 5;
    size_t param_end = query.find('&', param_start);
    if (param_end == std::string::npos) param_end = query.size();

    std::string filename_encoded = query.substr(param_start, param_end - param_start);
    if (filename_encoded.empty()) return;

    std::string filename_decoded;
    for (size_t i = 0; i < filename_encoded.size(); ++i) {
        if (filename_encoded[i] == '%' && i + 2 < filename_encoded.size()) {
            // 更严格的十六进制检查
            char hex1 = filename_encoded[i+1];
            char hex2 = filename_encoded[i+2];
            if (std::isxdigit(hex1) && std::isxdigit(hex2)) {
                int num = ConverHex(hex1) * 16 + ConverHex(hex2);
                filename_decoded += static_cast<char>(num);
                i += 2;
            } else {
                filename_decoded += filename_encoded[i]; // 保留无效的%
            }
        } else if (filename_encoded[i] == '+') {
            filename_decoded += ' '; // +号解码为空格
        } else {
            filename_decoded += filename_encoded[i];
        }
    }
    filePath_ = SanitizePath(filename_decoded);
    post_["file_name"] = filePath_;

}
void WebDisk::ParseFromDeletePath(){
    // DELETE /delete?file={filename}
    const std::string& path = request_->path();
    // 复用相同的解析逻辑[6](@ref)
    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) return;
    std::string query = path.substr(query_pos + 1);
    size_t file_param_pos = query.find("file=");
    if (file_param_pos == std::string::npos) return ;
    std::string filename_encoded = query.substr(file_param_pos + 5);
    std::string filename_decoded;
    for (size_t i = 0; i < filename_encoded.size(); ++i) {
        if (filename_encoded[i] == '%' && i + 2 < filename_encoded.size()) {
            int num = ConverHex(filename_encoded[i+1]) * 16 + 
                        ConverHex(filename_encoded[i+2]);
            filename_decoded += static_cast<char>(num);
            i += 2;
        } else if (filename_encoded[i] == '+') {
            filename_decoded += ' ';
        } else {
            filename_decoded += filename_encoded[i];
        }
    }
    // 安全过滤
    if (filename_decoded.find("..") == std::string::npos && 
        filename_decoded.find('/') != 0) {
        filePath_ = filename_decoded;
    }
}


int WebDisk::ConverHex(char ch){
    if(ch >= 'A' && ch<='F')return ch-'A'+10;
    if(ch >= 'a' && ch<='f')return ch-'a'+10;
    return ch-'0';
}

void WebDisk::Login(){
    if(request_->method()=="POST"){
        // RAII管理MySQL连接
        MYSQL* sql = nullptr;
        SqlConnRAII sql_guard(&sql, SqlConnPool::Instance());
        assert(sql);

        char query[256] = {0};
        snprintf(query, sizeof(query), 
            "SELECT password FROM user WHERE username='%s' LIMIT 1", 
            username_.c_str());
        if (mysql_query(sql, query)) return ;
        MYSQL_RES* res = mysql_store_result(sql);
        if (!res) return ;
        bool loginSucc = false;
        if (MYSQL_ROW row = mysql_fetch_row(res)) {
            loginSucc = (std::string(row[0]) == password_);
        }
        mysql_free_result(res);
        if (loginSucc) {
            std::string token = GenerateSessionToken();
            SessionManager::getInstance().addToken(token,username_);
            response_->SetHeader("Set-Cookie", "SESSION_TOKEN=" + token + "; HttpOnly");
            response_->SetStaticFile(srcDir_+"/webdisk.html");
        }else{
            response_->SetDynamicFile("LoginFail");
        }
    }
}
void WebDisk::Register(){
    if(request_->method()=="POST"){
        if (UserExists(username_)) {
            response_->SetCode(409); // 用户已存在
            return ;
        }

        MYSQL* sql = nullptr;
        SqlConnRAII sql_guard(&sql, SqlConnPool::Instance());
        assert(sql);

        char query[256] = {0};
        snprintf(query, sizeof(query),
            "INSERT INTO user(username, password) VALUES('%s','%s')",
            username_.c_str(), password_.c_str());

        bool success = (mysql_query(sql, query) == 0);
        // 创建用户专属目录
        if (success) {
            fs::create_directories(rootDir_ + "/" + username_); // 创建用户目录
            response_->SetDynamicFile("RegisterSuccess");
        }else{
            response_->SetDynamicFile("RegisterFail");
        }
        return ;
    }
}
void WebDisk::ListFiles() {
    std::string token = ParseSessionToken();
    if (!SessionManager::getInstance().queryToken(token)) {
        response_->SetCode(401);
        response_->SetDynamicFile(R"({"error":"Invalid session token"})");
        response_->SetHeader("Content-Type", "application/json");
        return;
    }

    std::ostringstream json;  // 使用流避免手动拼接
    json << "[";
    bool firstFile = true;

    for (const auto& entry : fs::directory_iterator(GetUserDir())) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        if (filename.empty() || filename[0] == '.') continue;

        // 仅转义关键字符（双引号和反斜杠）
        std::string escapedName;
        for (char c : filename) {
            if (c == '"' || c == '\\') escapedName += '\\';
            escapedName += c;
        }

        // 添加modified字段（必需）
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            entry.last_write_time().time_since_epoch()).count();

        if (!firstFile) json << ",";
        firstFile = false;

        json << "{"
             << "\"name\":\"" << escapedName << "\","
             << "\"size\":" << entry.file_size() << ","
             << "\"modified\":" << timestamp
             << "}";  // 移除尾逗号
    }
    json << "]";

    response_->SetDynamicFile(json.str());
    response_->SetHeader("Content-Type", "application/json");
    response_->SetCode(200);
}
void WebDisk::UploadFile(){
    std::string token = ParseSessionToken();
    if (!SessionManager::getInstance().queryToken(token)) {
        response_->SetCode(401);
        return ;
    }

    username_ = *SessionManager::getInstance().getSessionValue(token);
    userDir_ = rootDir_ + "/" + SanitizePath(username_);
    if (!fs::exists(userDir_)) fs::create_directories(userDir_);
    
    for (const auto& [key, value] : post_) {
        if (key.find("_filename") != std::string::npos) {
            std::string baseKey = key.substr(0, key.size() - 9); // 移除"_filename"后缀
            std::string filename = value;
            std::string content = post_[baseKey + "_content"];

            std::ofstream file(userDir_ + "/" + filename, std::ios::binary);
            if (file) {
                file.write(content.data(), content.size());
                LOG_INFO("File saved: %s (Size: %zu bytes)", filename.c_str(), content.size());
            } else {
                LOG_ERROR("Failed to write file: %s", filename.c_str());
            }
        }
    }
    response_->SetCode(200);
}

void WebDisk::DownloadFile(){
    std::string token = ParseSessionToken();
    if (!SessionManager::getInstance().queryToken(token)) {
        response_->SetCode(401);
        return;
    }

    // 修正：使用filePath_而不是post_["file_name"]
    // filePath_在ParseFromDownloadPath中已解析
    std::string filename = filePath_;
    if (filename.empty()) {
        response_->SetCode(400);
        return;
    }

    std::string filePath = GetUserDir() + "/" + SanitizePath(filename);
    response_->SetBigFile(filePath);
    response_->SetCode(200); // 成功时设置200状态码
}
void WebDisk::DeleteFile(){
    std::string token = ParseSessionToken();
    if (!SessionManager::getInstance().queryToken(token)) {
        return ;
    }
    std::string file_path = SanitizePath(userDir_ + "/" + post_["file_name"]);
    std::string trash_path = rootDir_ + "/trash/" + username_ + "/" + std::to_string(time(nullptr));
    fs::rename(file_path, trash_path); // 实际需处理跨设备移动
    return ;
}
void WebDisk::LogOut() {
    // 1. 获取会话令牌（从Cookie或Authorization头）
    std::string token = ParseSessionToken(); // 复用ParseSessionToken()方法[3](@ref)

    // 2. 验证会话有效性
    auto it = SessionManager::getInstance().getSessionValue(token);
    if (it == std::nullopt) {
        response_->SetCode(401); // 未登录用户无法登出
        response_->SetDynamicFile(R"({"error": "Invalid session token"})");
        return;
    }
    std::string username = *it; // 保存用户名用于审计
    // 3. 清理会话数据
    SessionManager::getInstance().removeToken(token);          // 移除服务器端会话记录
    username_.clear();               // 清除用户凭证
    password_.clear();
    userDir_.clear();
    // 4. 清理客户端会话标识
    response_->SetHeader("Set-Cookie", "session_token=; Max-Age=0; HttpOnly"); // 使Cookie过期[6](@ref)
    // 5. 记录审计日志（格式：时间戳, 用户名, 操作类型）
    std::time_t now = std::time(nullptr);
    std::string log_entry = std::to_string(now) + ", " + username + ", logout\n";
    
    std::ofstream audit_log("logs/audit.log", std::ios::app); // 追加模式
    if (audit_log) {
        audit_log << log_entry; // 写入审计日志[1](@ref)
    }
    // 6. 返回成功响应
    response_->SetCode(200);
    response_->SetDynamicFile(R"({"status": "logout_success"})");
}






bool WebDisk::UserExists(const std::string& user){
    MYSQL* sql = nullptr;
    SqlConnRAII sql_guard(&sql, SqlConnPool::Instance());
    
    char query[128] = {0};
    snprintf(query, sizeof(query),
            "SELECT 1 FROM user WHERE username='%s' LIMIT 1", 
            user.c_str());

    if (mysql_query(sql, query)) return false;
    MYSQL_RES* res = mysql_store_result(sql);
    bool exists = (res && mysql_num_rows(res) > 0);
    mysql_free_result(res);
    return exists;
}
std::string WebDisk::GetUserDir() {
    std::string token = ParseSessionToken();
    std::string usrname=*SessionManager::getInstance().getSessionValue(token);
    return rootDir_ + "/" + usrname;
}
std::string WebDisk::GenerateSessionToken(){
        return std::to_string(time(nullptr)) + "_" + std::to_string(rand());
}


std::string WebDisk::ParseSessionToken() {
    std::string cookie = request_->GetHeader("Cookie");
    size_t start = cookie.find("SESSION_TOKEN=");
    return (start != std::string::npos) ? 
            cookie.substr(start + 14, cookie.find(';', start) - start - 14) : "";
}

std::string WebDisk::SanitizePath(const std::string& path) {
    std::string clean = path;
    // 移除所有路径遍历尝试
    size_t pos;
    while ((pos = clean.find("../")) != std::string::npos) {
        clean.erase(pos, 3);
    }
    while ((pos = clean.find("..\\")) != std::string::npos) {
        clean.erase(pos, 3);
    }
    // 替换所有路径分隔符为下划线
    std::replace(clean.begin(), clean.end(), '/', '_');
    std::replace(clean.begin(), clean.end(), '\\', '_');
    // 移除特殊字符
    clean.erase(std::remove_if(clean.begin(), clean.end(), [](char c) {
        return c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
    }), clean.end());
    return clean;
}
void WebDisk::MergeChunks(const std::string& file_id, const std::string& file_name) {
    try {
        // 1. 准备分块目录和最终文件路径
        fs::path chunk_dir = rootDir_ + "/chunks/" + file_id;
        fs::path final_path = userDir_ + "/" + file_name;
        // 2. 打开最终文件
        std::ofstream final_file(final_path, std::ios::binary);
        if (!final_file.is_open()) {
            LOG_ERROR("无法创建最终文件: %s", final_path.c_str());
            return;
        }
        // 3. 按顺序合并所有分块
        for (int i = 0; ; i++) {
            fs::path chunk_path = chunk_dir / (std::to_string(i) + ".part");
            if (!fs::exists(chunk_path)) break;
            // 4. 读取分块内容并写入最终文件
            std::ifstream chunk_file(chunk_path, std::ios::binary);
            if (chunk_file.is_open()) {
                final_file << chunk_file.rdbuf();
                chunk_file.close();
            }
            // 5. 删除临时分块文件
            fs::remove(chunk_path);
        }
        // 6. 删除空的分块目录
        fs::remove(chunk_dir);
        
        LOG_INFO("文件合并成功: %s", final_path.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("合并分块失败: %s", e.what());
    }
}
std::string WebDisk::GenerateFileUUID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11); // 设置版本号位 (v4)

    std::stringstream ss;
    for (int i = 0; i < 32; i++) {
        if (i == 8 || i == 12 || i == 16 || i == 20) ss << "-";
        int val = (i == 12) ? dis2(gen) : dis(gen); // 第13位为版本号
        ss << std::hex << val;
    }
    return ss.str();
}
