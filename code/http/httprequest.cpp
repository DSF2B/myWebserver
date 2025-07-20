#include "httprequest.h"
/*
正则表达式
//g:正则表达式用//包裹，后面跟修饰符如g全局匹配,i忽略大小写,m多行匹配
/at./:.表示除换行符之外的任意一个字符 /a.t/ /.at/ 
/at[ce0]/:[]表示想要匹配的字符集合,atc,ate,at0
/at[a-zA-Z1-3]/:也可以用[-]表示一个范围
/at[^a-z]/:^表示取反，也就是不匹配[]中的内容 /at[^a-zA-Z]/表示字母以外的字符
^在[]外面表示匹配开头

预定义的字符类：
    \d:数字 /at\d/
    \D:非数字 /at\D/
    \w:字母数字或下划线
    \W:非字母数字或下划线
    \s:空格或者tab
    \S:非空格或者tab

位置和边界匹配
    ^:匹配开头  /^a/m:匹配每一行开头的a
    $:匹配结尾  /a$/:匹配结尾的a  /\.$/:\.转义.，匹配结尾的.
    \:转义\. \[ \+ \*
    /^at$/:该行只有这个at  at123at不会被匹配
    \b:单词的边界  /\bin/只匹配in开头的in /\bin\b/独立的in
    \B:非单词边界
    
    *:出现0或>1次
    +:>=1次
    ?:<=1次
    {n}:恰好n次
    {n,}:>=n次
    {n,m}:>=n,<=m次
    
    |:或 a(b|c)d:abd或acd
贪婪匹配:尽量匹配更多的字符
?恰好匹配:+?:恰好0次 {n,}?:恰好n次

分组:(ab){3}:ab3次
    (\d{4})[-/]?(\d{1,2})[-/]?(\d{1,2}):创建3个分组，后续可捕获提取这些部分 :2024-01-5 2024/4/25 2024425
    (?:)非捕获分组
    分组也可用于引用,\b([a-z])[a-z]*\1\b :\1代表第一个分组的内容,这个匹配头字母和尾字母相同的  adsesa edfgrge

前瞻和后顾
    匹配某些字符前面或后面的，但不匹配本身
    ?=:正向前瞻  \$(?=\d+):匹配数字前面的$  $100 $200 $abd $efd
    ?!:负向前瞻  分组内容取反:\$(?!\d+):匹配非数字前面的$
    ?<=:正向后顾  (?<=\$)\d+:匹配$后面的数字 ￥400 $500 #500
    ?<!:负向后顾  分组内容取反:(?<!\$)\b\d+\b:匹配非$后面的数字
*/


/*
GET / HTTP/1.1
Accept:text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,/;q=0.8,application/signed-exchange;v=b3;q=0.9
Accept-Encoding: gzip, deflate, br
Accept-Language: zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6
Connection: keep-alive
Host: www.baidu.com
Sec-Fetch-Dest: document
Sec-Fetch-Mode: navigate
Sec-Fetch-Site: none
Sec-Fetch-User: ?1
Upgrade-Insecure-Requests: 1
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/101.0.4951.41 Safari/537.36 Edg/101.0.1210.32
sec-ch-ua: " Not A;Brand";v=“99”, “Chromium”;v=“101”, “Microsoft Edge”;v=“101”
sec-ch-ua-mobile: ?0
sec-ch-ua-platform: “Windows”
*/
HttpRequest::HttpRequest(){
    Init();
}
void HttpRequest::Init(){
    state_=REQUEST_LINE;
    method_=path_=version_=body_="";
    header_.clear();
}


std::string HttpRequest::GetHeader(const std::string& key) const{
    assert(key!="");
    auto it=header_.find(key);
    if(it == header_.end()){
        return "";
    }
    return it->second;
}
std::string HttpRequest::GetHeader(const char* key) const{
    assert(key!="");
    auto it=header_.find(key);
    if(it == header_.end()){
        return "";
    }
    return it->second;
}
const std::string& HttpRequest::body() const {
    return body_; // 假设 body_ 已从 socket 读取并存储
}


// bool HttpRequest::ParseRequestLine_(const std::string& line){
//     std::regex pattern("^([^ ]+) ([^ ]+) HTTP/([^ ]+)$");
//     std::smatch subMatch;
//     if(std::regex_match(line,subMatch,pattern)){
//         method_=subMatch[0];
//         path_=subMatch[1];
//         version_=subMatch[2];
//         state_=HEADERS;
//         return true;
//     }
//     LOG_ERROR("Request line parse error");
//     return false;
// }
bool HttpRequest::ParseRequestLine_(const std::string& line) {
    // 查找第一个空格（方法结束位置）
    size_t pos1 = line.find(' ');
    if (pos1 == std::string::npos) return false;
    // 查找第二个空格（URI结束位置）
    size_t pos2 = line.find(' ', pos1 + 1);
    if (pos2 == std::string::npos) return false;
    // 检查协议前缀 "HTTP/"
    if (line.substr(pos2 + 1, 5) != "HTTP/") return false;
    // 提取各字段
    method_  = line.substr(0, pos1);
    path_    = line.substr(pos1 + 1, pos2 - pos1 - 1);
    version_ = line.substr(pos2 + 6); // 跳过 "HTTP/"
    state_   = HEADERS;
    return true;
}
// void HttpRequest::ParseHeader_(const std::string& line){
//     std::regex pattern("^([^:]): ?(.*)$");
//     std::smatch subMatch;
//     if(std::regex_match(line,subMatch,pattern)){
//         header_[subMatch[0]]=subMatch[1];
//     }else{
//         state_=BODY;
//     }
// }

void HttpRequest::ParseHeader_(const std::string& line) {
    size_t pos = line.find(':');
    if (pos == std::string::npos) {
        state_ = BODY;
        return;
    }
    // 提取 Key
    std::string key = line.substr(0, pos);
    // 提取 Value (跳过冒号和空格)
    size_t value_start = pos + 1;
    while (value_start < line.size() && line[value_start] == ' ') {
        value_start++;
    }
    std::string value = line.substr(value_start);
    // 存储键值对
    header_[key] = value;
}

void HttpRequest::ParseBody_(Buffer& buff){
    body_ = buff.RetrieveAllToStr();
    state_=FINISH;
    LOG_DEBUG("Body:%s,len:%d",body_,body_.size());
}
bool HttpRequest::parse(Buffer& buff){
    const char CRLF[]="\r\n";
    if(buff.WritableBytes() <=0){
        return false;
    } 

    while(buff.ReadableBytes() && state_!=FINISH){
        const char* lineEnd=std::search(buff.Peek(),buff.BeginWriteConst(),CRLF,CRLF+2);
        if(lineEnd == buff.BeginWrite())break;//读到了写区，已经读完
        std::string line(buff.Peek(), lineEnd);
        buff.RetrieveUntil(lineEnd+2);//下一行
        switch(state_){
            case REQUEST_LINE:{
                if(!ParseRequestLine_(line)){
                    return false;
                }
                break;
            }
            case HEADERS:{
                if(line.empty()){
                    state_=BODY;
                    if(buff.ReadableBytes() <=2){
                        state_=FINISH;
                        break;
                    }
                    ParseBody_(buff);
                }else{
                    ParseHeader_(line);
                }   
                break;
            }
            default:{
                break;
            }
        }

    }
    LOG_DEBUG("[%s], [%s], [%s]",method_.c_str(),path_.c_str(),version_.c_str());
    return true;
}


bool HttpRequest::IsKeepAlive() const{
    auto it = header_.find("Connection");
    if(it==header_.end()){
        return false;
    }else{
        return it->second == "keep-alive" && version_ == "1.1";
    }
}
std::string HttpRequest::path() const{return path_;}
std::string& HttpRequest::path(){return path_;}
std::string HttpRequest::method() const{return method_;}
std::string HttpRequest::version() const{return version_;}
