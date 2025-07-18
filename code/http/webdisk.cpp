#include "webdisk.h"
WebDisk::WebDisk(){
    rootDir_ = "./data/webdisk_users";
    fs::create_directories(rootDir_);
}
const std::unordered_map<std::string, int> WebDisk::FUNC_TAG{
    {"/login",0},{"/register",1},{"/list",2},{"upload",3},{"download",4},{"delete",5}
};
void WebDisk::Init(const HttpRequest& req, HttpResponse& res){
    request_ = req;
    response_ = res;
}
void WebDisk::Handle(){
    auto it=FUNC_TAG.find(request_.path());
    if(it==FUNC_TAG.end()){
        response_.SetCode(404);
        response_.SetBody("Not Found");
        return;
    }
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
    default:
        break;
    }
}
bool WebDisk::Login(){
    username_ = request_.GetPost("username");
    password_ = request_.GetPost("password");

    // RAII管理MySQL连接
    MYSQL* sql = nullptr;
    SqlConnRAII sql_guard(&sql, SqlConnPool::Instance());
    assert(sql);

    char query[256] = {0};
    snprintf(query, sizeof(query), 
        "SELECT password FROM user WHERE username='%s' LIMIT 1", 
        username_.c_str());
    if (mysql_query(sql, query)) return false;
    MYSQL_RES* res = mysql_store_result(sql);
    if (!res) return false;
    bool loginSucc = false;
    if (MYSQL_ROW row = mysql_fetch_row(res)) {
        loginSucc = (std::string(row[0]) == password_);
    }
    mysql_free_result(res);
    if (loginSucc) {
        std::string token = GenerateSessionToken();
        {
            std::lock_guard<std::mutex> lock(mtx_);
            sessions_[token] = username_;
        }
        response_.SetHeader("Set-Cookie", "SESSION_TOKEN=" + token + "; HttpOnly");
    }
    return loginSucc;
}
bool WebDisk::Register(){
    username_ = request_.GetPost("username");
    password_ = request_.GetPost("password");

    if (UserExists(username_)) {
        response_.SetCode(409); // 用户已存在
        return false;
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
    }
    return success;
}
bool WebDisk::ListFiles(){
    std::string token = ParseSessionToken();
    if (sessions_.find(token) == sessions_.end()) {
        return false;
    }

    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(GetUserDir())) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().filename().string());
        }
    }
    // 生成JSON响应
    std::string body="";
    body += "[";
    for (const auto& file : files) {
        body += "\"" + file + "\",";
    }
    if (!files.empty()) body.pop_back(); // 移除末尾逗号
    body += "]";
    response_.SetBody(body);
    response_.SetHeader("Content-Type", "application/json");
    return true;
}
bool WebDisk::UploadFile(){
    std::string token = ParseSessionToken();
    if (sessions_.find(token) == sessions_.end()) return false;

    std::string file_name = SanitizePath(request_.GetPost("file_name"));
    int chunk_index = std::stoi(request_.GetPost("chunk_index"));
    int total_chunks = std::stoi(request_.GetPost("total_chunks"));

    // 写入分块文件
    fs::path chunk_path = userDir_ + "/tmp/" + file_name + ".part" + std::to_string(chunk_index);
    std::ofstream chunk_file(chunk_path, std::ios::binary);
    chunk_file.write(request_.GetBody().data(), request_.GetBody().size());

    // 最终块触发合并
    if (chunk_index == total_chunks - 1) {
        MergeChunks(userDir_ + "/" + file_name, 
                   userDir_ + "/tmp/", 
                   file_name);
    }
    return true;
}
bool WebDisk::DownloadFile(){
    std::string token = ParseSessionToken();
    if (sessions_.find(token) == sessions_.end()) return false;

    std::string filePath = GetUserDir() + "/" + SanitizePath(request_.GetPost("file_name"));
    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd == -1) {
        response_.SetCode(404);
        return false;
    }
    //************** */
    //放到response实现
    //************* */
    // 零拷贝传输
    struct stat file_stat;
    fstat(fd, &file_stat);
    response_.SetHeader("Content-Length", std::to_string(file_stat.st_size));
    off_t offset = 0;
    sendfile(response_.GetSocketFD(), fd, &offset, file_stat.st_size);
    close(fd);
    return true;
}
bool WebDisk::DeleteFile(){
    std::string token = ParseSessionToken();
    if (sessions_.find(token) == sessions_.end()) return false;

    std::string file_path = SanitizePath(userDir_ + "/" + request_.GetPost("file_name"));
    std::string trash_path = rootDir_ + "/trash/" + username_ + "/" + std::to_string(time(nullptr));
    fs::rename(file_path, trash_path); // 实际需处理跨设备移动
    return true;
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
    std::lock_guard<std::mutex> lock(mtx_);
    return rootDir_ + "/" + sessions_[token];
}
std::string WebDisk::GenerateSessionToken(){
        return std::to_string(time(nullptr)) + "_" + std::to_string(rand());
}


std::string WebDisk::ParseSessionToken() {
    std::string cookie = request_.GetHeader("Cookie");
    size_t start = cookie.find("SESSION_TOKEN=");
    return (start != std::string::npos) ? 
            cookie.substr(start + 14, cookie.find(';', start) - start - 14) : "";
}

std::string WebDisk::SanitizePath(const std::string& path){
    std::string clean = path;
    size_t pos;
    while ((pos = clean.find("../")) != std::string::npos) {
        clean.erase(pos, 3);
    }
    return clean;
}
void WebDisk::MergeChunks(const std::string& output_path, const std::string& tmp_dir, const std::string& base_name){
    std::ofstream out_file(output_path, std::ios::binary);
    for (int i = 0; ; ++i) {
        std::string chunk_path = tmp_dir + base_name + ".part" + std::to_string(i);
        if (!fs::exists(chunk_path)) break;
        
        std::ifstream chunk(chunk_path, std::ios::binary);
        out_file << chunk.rdbuf();
        fs::remove(chunk_path); // 删除临时分块
    }
}
