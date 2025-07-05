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
    srcDir_="";
    mmFile_=nullptr;
    mmFileStat_={0};

}
HttpResponse::~HttpResponse(){
    UnmapFile();
}

void HttpResponse::Init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1){
    assert(srcDir!="");
    srcDir_=srcDir;
    path_=path;
    isKeepAlive_=isKeepAlive;
    code_=code;

    if(mmFile_){
        UnmapFile();
    }
    mmFile_=nullptr;
    mmFileStat_={0};
}
void HttpResponse::MakeResponse(Buffer& buff){
    if(stat((srcDir_+path_).data(), &mmFileStat_)<0 || S_ISDIR(mmFileStat_.st_mode)){
        //通过路径 srcDir_ + path_ 获取文件元信息（如类型、权限、大小等），存储到结构体 mmFileStat_ 中。若文件不存在或路径无效，stat返回负值（通常为-1）
        // 文件不存在或目标为目录
        code_=404;
    }else if(!(mmFileStat_.st_mode & S_IROTH)){
        //// 文件不可读（权限不足）
        code_=403;
    }else if(code_ == -1){
        code_=200;
    }

    ErrorHtml_();
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
char* HttpResponse::File(){
    return mmFile_;
}
size_t HttpResponse::FileLen() const{
    return mmFileStat_.st_size;
}

int HttpResponse::Code() const { 
    return code_; 
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
}
// <html>
//       <head></head>
//       <body>
//             <!--body goes here-->
//       </body>
// </html>
void HttpResponse::AddContent_(Buffer &buff){
    int srcFd=open((srcDir_+path_).data(),O_RDONLY);
    if(srcFd<0){
        ErrorContent(buff,"Fild NotFound");
        return ;
    }
    LOG_DEBUG("file path: %s",(srcDir_+path_).data());
    int* mmRet=(int*)mmap(0,mmFileStat_.st_size,PROT_READ,MAP_PRIVATE,srcFd,0);
    if(*mmRet==-1){
        ErrorContent(buff,"File NotFound");
        return ;
    }
    mmFile_=(char*)mmRet;
    close(srcFd);
    buff.Append("Content-length: " + std::to_string(mmFileStat_.st_size) + "\r\n\r\n");
}

void HttpResponse::ErrorHtml_(){
    auto it =CODE_PATH.find(code_);
    if(it!=CODE_PATH.end()){
        path_=it->second;
        stat((srcDir_+path_).data(),&mmFileStat_);
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