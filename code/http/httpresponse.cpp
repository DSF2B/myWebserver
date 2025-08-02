#include "httpresponse.h"
const std::unordered_map<std::string, std::string> HttpResponse::SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".xml",   "text/xml" }, 
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css "},
    { ".js",    "text/javascript "},
};
const std::unordered_map<int, std::string> HttpResponse::CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
};

const std::unordered_map<int, std::string> HttpResponse::CODE_PATH = {
    { 400, "/400.html" },
    { 403, "/403.html" },
    { 404, "/404.html" },
};


HttpResponse::HttpResponse(){
    code_=-1;
    isKeepAlive_=false;
    path_="";
    mmFile_=nullptr;
    fileFd_=-1;
    mmFileStat_={0};
    userFileSize_=0;
}
HttpResponse::~HttpResponse(){
    UnmapFile();
    if (fileFd_ != -1) {
        close(fileFd_); // 析构时关闭文件
    }
}

void HttpResponse::Init(bool isKeepAlive, int code){
    isKeepAlive_=isKeepAlive;
    code_=code;
    if(mmFile_){
        UnmapFile();
    }
    mmFile_=nullptr;
    if (fileFd_ != -1) {
        close(fileFd_); // 析构时关闭文件
    }
    fileFd_=-1;
    mmFileStat_={0};
}
void HttpResponse::MakeResponse(Buffer& buff){
    // 如果状态码未设置，根据发送类型设置默认状态码
    if(code_ == -1){
        code_ = 200; // 默认成功状态码
    }

    // 只有在出现错误状态码时才需要错误页面处理
    if(code_ >= 400) {
        ErrorHtml_();
    }
    
    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}
void HttpResponse::UnmapFile(){
    if(mmFile_){
        munmap(mmFile_,mmFileStat_.st_size);
        mmFile_=nullptr;
    }
}
void HttpResponse::CloseFd(){
    if(fileFd_!=-1){
        close(fileFd_);
    }
}
char* HttpResponse::mmFile(){
    return mmFile_;
}
int HttpResponse::File(){
    return fileFd_;
}
size_t HttpResponse::FileLen() const{
    return mmFileStat_.st_size;
}

int HttpResponse::Code() const { 
    return code_; 
}
SendFileType HttpResponse::sendFileType(){
    return sendFileType_;
}
std::string& HttpResponse::body(){
    return body_;
}
void HttpResponse::SetCode(const int& code){
    code_=code;
}

void HttpResponse::SetHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}
void HttpResponse::SetStaticFile(const std::string& path){
    path_=path;
    sendFileType_=SendFileType::StaticWebPage;
    
    // 检查静态文件是否存在和可访问
    if (stat(path.c_str(), &mmFileStat_) < 0) {
        LOG_ERROR("Static file not found: %s", path.c_str());
        code_ = 404;
        return;
    }
    if (S_ISDIR(mmFileStat_.st_mode)) {
        LOG_ERROR("Path is directory, not file: %s", path.c_str());
        code_ = 403;
        return;
    }
    if (!(mmFileStat_.st_mode & S_IROTH)) {
        LOG_ERROR("Static file not readable: %s", path.c_str());
        code_ = 403;
        return;
    }
}
void HttpResponse::SetDynamicFile(const std::string& body){
    body_=body;
    sendFileType_=SendFileType::DynamicWebPage;
}
void HttpResponse::SetBigFile(const std::string& userFilePath){
    userFilePath_=userFilePath;
    sendFileType_=SendFileType::UserData;
    if (stat(userFilePath.c_str(), &mmFileStat_) < 0) { // 文件不存在或不可访问
        LOG_ERROR("File not found: %s", userFilePath.c_str());
        code_ = 404; // 自动设置404状态码
        return;
    }
    if (!S_ISREG(mmFileStat_.st_mode)) { // 非普通文件（如目录）
        LOG_ERROR("Invalid file type: %s", userFilePath.c_str());
        code_ = 400; // Bad Request
        return;
    }
    userFileSize_ = mmFileStat_.st_size; // 记录文件大小

    std::string filename = userFilePath;
    size_t pos = filename.find_last_of("/\\");
    if (pos != std::string::npos) {
        filename = filename.substr(pos + 1);
    }
    // 2. 设置必要响应头（后续在MakeResponse中使用）
    headers_["Content-Type"] = "application/octet-stream";
    headers_["Content-Disposition"] = "attachment; filename=\"" + filename + "\"";
}


int HttpResponse::GetSocketFD() const { 
    return fileFd_; 
}
// HTTP/1.1 200 OK
void HttpResponse::AddStateLine_(Buffer &buff){
    std::string status;
    auto it=CODE_STATUS.find(code_);
    if(it!=CODE_STATUS.end()){
        status=it->second;
    }else{
        code_=400;
        status="Bad Request";
    }
    buff.Append("HTTP/1.1 "+std::to_string(code_)+" "+status+"\r\n");
}
// Date: Fri, 22 May 2009 06:07:21 GMT
// Content-Type: text/html; charset=UTF-8
// \r\n
void HttpResponse::AddHeader_(Buffer &buff){
    buff.Append("Connection: ");
    if(isKeepAlive_){
        buff.Append("keep-alive\r\n");
        buff.Append("keep-alive: max=6, timeout=120\r\n");
    }else{
        buff.Append("close\r\n");
    }
    buff.Append("Connect-Type: "+GetFileType_() + "\r\n");
    size_t contentLength = 0;
    switch (sendFileType_) {
        case SendFileType::DynamicWebPage:
            contentLength = body_.size(); // 动态内容长度
            break;
        case SendFileType::StaticWebPage:
            contentLength = mmFileStat_.st_size; // 静态文件长度
            break;
        case SendFileType::UserData:
            contentLength = userFileSize_; // 用户数据文件大小（需提前获取）
            break;
    }
    buff.Append("Content-Length: " + std::to_string(contentLength) + "\r\n");
    for (const auto& [key, value] : headers_) {
        buff.Append(key + ": " + value + "\r\n");
    }
    buff.Append("\r\n"); // 头部结束标记

}
// <html>
//       <head></head>
//       <body>
//             <!--body goes here-->
//       </body>
// </html>

// void HttpResponse::AddContent_(Buffer &buff){
//     fileFd_=open((srcDir_+path_).data(),O_RDONLY);
//     if(fileFd_<0){
//         ErrorContent(buff,"File NotFound");
//         return ;
//     }else {
//         fstat(fileFd_, &mmFileStat_);
//     }
//     LOG_DEBUG("file path: %s",(srcDir_+path_).data());
//     // int* mmRet=(int*)mmap(0,mmFileStat_.st_size,PROT_READ,MAP_PRIVATE,srcFd,0);
//     // if(*mmRet==-1){
//     //     ErrorContent(buff,"File NotFound");
//     //     return ;
//     // }
//     // mmFile_=(char*)mmRet;
//     buff.Append("Content-length: " + std::to_string(mmFileStat_.st_size) + "\r\n\r\n");
// }
void HttpResponse::AddContent_(Buffer& buff) {
    if (!body_.empty() && sendFileType_==SendFileType::DynamicWebPage) {
        // httpconn中body_写入iov[1]
        return;
    }
    else if (path_ != "" && sendFileType_==SendFileType::StaticWebPage) {
        // 静态文件：检查并打开文件
        
        fileFd_=open((path_).data(),O_RDONLY);
        if(fileFd_<0){
            LOG_ERROR("Failed to open static file: %s", path_.c_str());
            code_ = 404; // 设置错误状态码
            return;
        }
        LOG_DEBUG("Static file opened: %s", path_.c_str());
        if(fstat(fileFd_, &mmFileStat_) < 0) {
            LOG_ERROR("fstat failed for file: %s", path_.c_str());
            close(fileFd_);
            fileFd_ = -1;
            code_ = 404;
            return;
        }        
        void* mmRet = mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, fileFd_, 0);
        // if(*mmRet == -1) {
        //     ErrorContent(buff, "File NotFound!");
        //     return;
        // }
        mmFile_ = (char*)mmRet;
        CloseFd();
        fileFd_=-1;
    }
    else if (userFilePath_!="" && sendFileType_==SendFileType::UserData) {
        // 用户文件：SetBigFile已检查过，直接打开
        fileFd_=open((userFilePath_).data(),O_RDONLY);
        if(fileFd_<0){
            LOG_ERROR("Failed to open user file: %s", userFilePath_.c_str());
            code_ = 404; // 设置错误状态码  
            return;
        }
        LOG_DEBUG("User file opened: %s", userFilePath_.c_str());
    }
}
void HttpResponse::ErrorHtml_(){
    auto it =CODE_PATH.find(code_);
    if(it!=CODE_PATH.end()){
        path_=it->second;
        stat((path_).data(),&mmFileStat_);
    }
}
// <html>
//       <head></head>
//       <body>
//             <!--body goes here-->
//       </body>
// </html>
void HttpResponse::ErrorContent(Buffer& buff, std::string message){
    std::string body;
    std::string status;
    body+="<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    auto it=CODE_STATUS.find(code_);
    if(it!=CODE_STATUS.end()){
        status=it->second;
    }else{
        status="BAD REQUEST";
    }
    body+= std::to_string(code_) + " : " + status  + "\n";
    body+="<p>"+message+"</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";
    buff.Append("Content-length: "+std::to_string(body.size()) +"\r\n\r\n");
    buff.Append(body);
}
std::string HttpResponse::GetFileType_(){
    // 得到请求的文件类型 
    std::string::size_type idx=path_.find_last_of('.');
    if(idx==std::string::npos){
        return "text/plain";
    }
    std::string suffix=path_.substr(idx);
    auto it=SUFFIX_TYPE.find(suffix);
    if(it!=SUFFIX_TYPE.end()){
        return it->second;
    }
    return "text/plain";
}
