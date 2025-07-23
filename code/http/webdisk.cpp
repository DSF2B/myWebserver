#include "webdisk.h"
WebDisk::WebDisk(std::shared_ptr<HttpRequest> req, 
    std::shared_ptr<HttpResponse> res,
    const std::string srcDir){
    rootDir_ = "./usrdata/webdisk_users";
    fs::create_directories(rootDir_);
    fs::create_directories(rootDir_ + "/tmp");
    fs::create_directories(rootDir_ + "/chunks");
    request_ = req;
    response_ = res;
    
    username_="";
    password_="";
    srcDir_=srcDir;
    post_.clear();
    userDir_="";

}
// const std::unordered_map<std::string, int> WebDisk::FUNC_TAG{
//     {"/login",0},{"/register",1},{"/files",2},{"/upload",3},{"/download",4},
//     {"/delete",5},{"/logout",6},{"/upload_chunk",7},{"/merge_chunks",8}
// };
const std::unordered_map<std::string, int> WebDisk::FUNC_TAG{
    {"/api2/login",0},        // POST /api2/login - 执行登录
    {"/api2/register",1},     // POST /api2/register - 执行注册  
    {"/api2/files",2},        // GET /api2/files - 获取文件列表
    {"/api2/upload",3},       // POST /api2/upload - 文件上传
    {"/api2/download",4},     // GET /api2/download - 文件下载
    {"/api2/delete",5},       // DELETE /api2/delete - 删除文件
    {"/api2/logout",6},       // POST /api2/logout - 退出登录
    {"/api2/upload_chunk",7}, // POST /api2/upload_chunk - 分块上传
    {"/api2/merge_chunks",8}  // POST /api2/merge_chunks - 合并分块
};
// // 文件列表
// GET /api2/files
// // 文件上传
// POST /upload
// // 文件下载
// GET /download?file={filename}
// // 文件删除
// DELETE /delete?file={filename}
// // 退出登录
// POST /logout
const std::unordered_set<std::string> WebDisk::DEFAULT_HTML{
    "/index","/register","/login","/welcome","/video","/picture","/webdisk"
};

void WebDisk::Handle(){
    ParsePath();
    //path_:
    // "/index","/register","/login","/welcome","/video","/picture"
    // /api2/files  /upload  /download  /delete  /logout

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
        break;
    case 7:
        UploadChunk();
        break;
    case 8:
        MergeChunks();
        break;
    default:
        break;
    }
}
void WebDisk::ParsePath(){
    path_=request_->path();
    size_t query_pos = path_.find('?');
    ///api2/download？
    if(query_pos !=std::string::npos){
        path_=path_.substr(0,query_pos);
    }
    
    if(path_ == "/"){
        path_="/index";
        response_->SetStaticFile(srcDir_+"/index.html");
    }else if(path_=="/api2/download"){
        ParseFromDownloadPath();
    }else if(path_=="/api2/delete"){
        ParseFromDeletePath();
    }else{
        auto it=FUNC_TAG.find(path_);
        if(it != FUNC_TAG.end()){
            LOG_DEBUG("API route detected: [%s]", path_.c_str());
            return;  // API路由，交给Handle()处理，不设置静态文件
        }else{
            auto it=DEFAULT_HTML.find(path_);
            if(it!=DEFAULT_HTML.end()){
                response_->SetStaticFile(srcDir_+it->data()+".html");
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
        // 3. 跳过边界标记本身
        partStart += boundary.size();
        if (body.substr(partStart, 2) == "--"){ // 处理结束边界
            break;
        }
        // 4. 解析分片头部（改进换行符处理）
        partStart += 2; // 跳过\r\n

        size_t headerStart=partStart;
        size_t headerEnd = body.find("\r\n\r\n", partStart);
        if (headerEnd == std::string::npos) break;
        size_t headerLength = headerEnd - headerStart;
        std::string headers = body.substr(
            headerStart, 
            headerLength
        );

        // 4. 解析header
        std::string name, filename;
        size_t namePos = headers.find("name=\"");
        if (namePos != std::string::npos) {
            size_t nameEnd = headers.find("\"", namePos + 6);
            name = headers.substr(namePos + 6, nameEnd - namePos - 6);
        }

        // 5. 提取正文
        size_t contentStart = headerEnd + 4;
        size_t contentEnd = body.find(boundary, contentStart);
        if (contentEnd == std::string::npos){
            contentEnd = body.size();
        }
        size_t contentLength = contentEnd - contentStart;
        // 处理内容末尾的\r\n
        if (contentLength >= 2 && 
            body[contentEnd - 2] == '\r' && 
            body[contentEnd - 1] == '\n') {
            contentLength -= 2;
        }
        std::string content = body.substr(
            contentStart,
            contentLength  // 减去末尾\r\n
        );
        
        // 6. 存储数据[3]
        if (!name.empty()) {
            post_[name] = content;
        }
        start = contentEnd;
    }
    // std::cout<<"123"<<std::endl;
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
    fileName_ = SanitizePath(filename_decoded);

}
void WebDisk::ParseFromDeletePath(){
    // DELETE /delete?file={filename}
    const std::string& path = request_->path();
    // 复用相同的解析逻辑[6](@ref)
    size_t query_pos = path.find('?');
    if (query_pos == std::string::npos) return;
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
        fileName_ = filename_decoded;
    }
}


int WebDisk::ConverHex(char ch){
    if(ch >= 'A' && ch<='F')return ch-'A'+10;
    if(ch >= 'a' && ch<='f')return ch-'a'+10;
    return ch-'0';
}

void WebDisk::Login(){
    if(request_->method()=="POST"){
        // 先清理可能存在的旧会话
        
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
            // 生成新的会话令牌
            std::string token = GenerateSessionToken();
            SessionManager::getInstance().addToken(token, username_);
            
            response_->SetCode(200);
            response_->SetHeader("Set-Cookie", "SESSION_TOKEN=" + token + "; Path=/");

            response_->SetStaticFile(srcDir_+"/webdisk.html");
        } else {
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
            response_->SetCode(200); // 登录成功
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


    response_->SetHeader("Content-Type", "application/json");
    response_->SetCode(200);
    response_->SetDynamicFile(json.str());
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

    std::string filename = post_["filename"];
    std::string content = post_["chunk"];
    
    std::ofstream file(userDir_ + "/" + filename, std::ios::binary);
    if (file) {
        file.write(content.data(), content.size());
        LOG_INFO("File saved: %s (Size: %zu bytes)", filename.c_str(), content.size());
    } else {
        LOG_ERROR("Failed to write file: %s", filename.c_str());
    }

    response_->SetCode(200);
    response_->SetDynamicFile(R"({"status": "upload_success"})");
}

void WebDisk::DownloadFile(){
    std::string token = ParseSessionToken();
    if (!SessionManager::getInstance().queryToken(token)) {
        response_->SetCode(401);
        return;
    }

    // fileName_在ParseFromDownloadPath中已解析
    std::string filename = fileName_;
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
    std::string file_path = GetUserDir() + "/" + SanitizePath(fileName_);
    std::string trash_path = rootDir_ + "/trash/" + username_ + "/" + std::to_string(time(nullptr));
    fs::rename(file_path, trash_path); // 实际需处理跨设备移动
    response_->SetCode(200); // 成功时设置200状态码
    response_->SetDynamicFile(R"({"status": "delete_success"})");
    return ;
}
void WebDisk::LogOut() {
    // 1. 获取会话令牌
    std::string token = ParseSessionToken();

    // 2. 如果有令牌就清理，没有也继续执行
    if (!token.empty()) {
        auto it = SessionManager::getInstance().getSessionValue(token);
        if (it != std::nullopt) {
            std::string username = *it;
            
            // 记录审计日志
            std::time_t now = std::time(nullptr);
            std::string log_entry = std::to_string(now) + ", " + username + ", logout\n";
            
            std::ofstream audit_log("logs/audit.log", std::ios::app);
            if (audit_log) {
                audit_log << log_entry;
            }
        }
        
        // 清理服务器端会话
        SessionManager::getInstance().removeToken(token);
    }
    
    // 3. 清理当前对象状态
    username_.clear();
    password_.clear();
    userDir_.clear();
    // 5. 返回登录页面
    response_->SetCode(200);
    // response_->SetStaticFile(srcDir_+"/login.html");
    response_->SetDynamicFile("LogOutSuccess");
}

void WebDisk::UploadChunk() {
    std::string token = ParseSessionToken();
    if (!SessionManager::getInstance().queryToken(token)) {
        response_->SetCode(401);
        return;
    }
    username_ = *SessionManager::getInstance().getSessionValue(token);
    // 从POST数据中获取分块信息
    std::string fileId = post_["fileId"];
    std::string fileName = post_["fileName"];
    std::string chunkIndex = post_["chunkIndex"];
    std::string chunkContent = post_["chunk"];
    LOG_DEBUG("UploadChunk - fileId: [%s], fileName: [%s], chunkIndex: [%s], chunkSize: %zu", 
              fileId.c_str(), fileName.c_str(), chunkIndex.c_str(), chunkContent.size());    
    if (fileId.empty() || fileName.empty() || chunkIndex.empty()) {
        response_->SetCode(400);
        return;
    }
    
    // 创建分块存储目录
    std::string chunkDir = rootDir_ + "/chunks/" + SanitizePath(fileId);
    fs::create_directories(chunkDir);
    
    // 保存分块文件
    std::string chunkPath = chunkDir + "/" + chunkIndex + ".part";
    std::ofstream chunkFile(chunkPath, std::ios::binary);
    if (chunkFile) {
        chunkFile.write(chunkContent.data(), chunkContent.size());
        chunkFile.close();
        
        response_->SetCode(200);
        response_->SetDynamicFile(R"({"status": "chunk_uploaded"})");
        LOG_INFO("Chunk %s uploaded for file %s", chunkIndex.c_str(), fileName.c_str());
    } else {
        response_->SetCode(500);
        response_->SetDynamicFile(R"({"error": "Failed to save chunk"})");
    }
}

void WebDisk::MergeChunks() {
    std::string token = ParseSessionToken();
    if (!SessionManager::getInstance().queryToken(token)) {
        response_->SetCode(401);
        return;
    }

    username_ = *SessionManager::getInstance().getSessionValue(token);
    userDir_ = rootDir_ + "/" + SanitizePath(username_);
    
    // 解析JSON请求体
    std::string body = request_->body();
    size_t fileIdPos = body.find("\"fileId\":\"");
    size_t fileNamePos = body.find("\"fileName\":\"");
    
    if (fileIdPos == std::string::npos || fileNamePos == std::string::npos) {
        response_->SetCode(400);
        return;
    }
    
    std::string fileId = extractJsonString(body, fileIdPos + 10);
    std::string fileName = extractJsonString(body, fileNamePos + 12);
    
    if (fileId.empty() || fileName.empty()) {
        response_->SetCode(400);
        return;
    }
    
    // 执行文件合并
    try {
        fs::path chunk_dir = rootDir_ + "/chunks/" + fileId;
        fs::path final_path = userDir_ + "/" + fileName;
        
        std::ofstream final_file(final_path, std::ios::binary);
        if (!final_file.is_open()) {
            LOG_ERROR("无法创建最终文件: %s", final_path.c_str());
            response_->SetCode(500);
            response_->SetDynamicFile(R"({"error": "Failed to create file"})");
            return;
        }
        
        // 按顺序合并所有分块
        for (int i = 0; ; i++) {
            fs::path chunk_path = chunk_dir / (std::to_string(i) + ".part");
            if (!fs::exists(chunk_path)) break;
            
            std::ifstream chunk_file(chunk_path, std::ios::binary);
            if (chunk_file.is_open()) {
                final_file << chunk_file.rdbuf();
                chunk_file.close();
            }
            
            fs::remove(chunk_path);
        }
        
        fs::remove(chunk_dir);
        
        LOG_INFO("文件合并成功: %s", final_path.c_str());
        response_->SetCode(200);
        response_->SetDynamicFile(R"({"status": "merge_success"})");
        
    } catch (const std::exception& e) {
        LOG_ERROR("合并分块失败: %s", e.what());
        response_->SetCode(500);
        response_->SetDynamicFile(R"({"error": "Merge failed"})");
    }
}

std::string WebDisk::extractJsonString(const std::string& json, size_t start) {
    size_t end = json.find("\"", start);
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
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
