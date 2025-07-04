#include "httpresponse.h"

HttpResponse::HttpResponse(){

}
HttpResponse::~HttpResponse(){

}

void HttpResponse::Init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1){
    int code_;
    bool isKeepAlive_;

    std::string path_;
    std::string srcDir_;
    
    char* mmFile_; 
    struct stat mmFileStat_;

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;
    static const std::unordered_map<int, std::string> CODE_STATUS;
    static const std::unordered_map<int, std::string> CODE_PATH;
}
void HttpResponse::MakeResponse(Buffer& buff){

}
void HttpResponse::UnmapFile(){

}
char* HttpResponse::File(){

}
size_t HttpResponse::FileLen() const{

}
void HttpResponse::ErrorContent(Buffer& buff, std::string message){

}
int HttpResponse::Code() const { 
    return code_; 
}

void HttpResponse::AddStateLine_(Buffer &buff){

}
void HttpResponse::AddHeader_(Buffer &buff){

}
void HttpResponse::AddContent_(Buffer &buff){

}

void HttpResponse::ErrorHtml_(){

}
std::string HttpResponse::GetFileType_(){
    
}