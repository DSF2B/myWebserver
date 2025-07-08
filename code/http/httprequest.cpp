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

HttpRequest::HttpRequest(){
    Init();
}
const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML{
    "/index","register","/login","welcome","video","picture"
};
const std::unordered_map<std::string, int> HttpRequest::DEFAULT_HTML_TAG{
    {"/register.html",0},{"/login.html",1}
};
void HttpRequest::Init(){
    state_=REQUEST_LINE;
    method_=path_=version_=body_="";
    header_.clear();
    post_.clear();
}

std::string HttpRequest::GetPost(const std::string& key) const{
    assert(key!="");
    auto it=post_.find(key);
    if(it == post_.end()){
        return "";
    }
    return it->second;
}
std::string HttpRequest::GetPost(const char* key) const{
    assert(key!=nullptr);
    auto it=post_.find(key);
    if(it == post_.end()){
        return "";
    }
    return it->second;
}

void HttpRequest::ParsePath_(){
    if(path_ == "/"){
        path_="/index.html";
    }else{
        for(auto& item : DEFAULT_HTML){
            if(path_ == item){
                path_+=".html";
                return;
            }
        }
    }
}
// 处理post请求
void HttpRequest::ParsePost_(){

}
// 从url中解析编码
void HttpRequest::ParseFromUrlencoded_(){
    //username=john%20doe&password=123


}

bool HttpRequest::ParseRequestLine_(const std::string& line){

}
void HttpRequest::ParseHeader_(const std::string& line){

}
void HttpRequest::ParseBody_(const std::string& line){
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded"){
        ParseFromUrlencoded_();     // 解析POST请求体中的url
        auto it=DEFAULT_HTML_TAG.find(path_);
        if(it!=DEFAULT_HTML_TAG.end()){
            int tag=it->second;
            LOG_DEBUG("Tag:%d",tag);
            if(tag==0||tag==1){
                bool isLogin=(tag==1);
                if(UserVerify(post_["username"],post_["password"],isLogin)){
                    path_="/welcome.html";
                }else{
                    path_="error.html";
                }
            }
        }

    }
}
bool HttpRequest::parse(Buffer& buff){

}
bool HttpRequest::UserVerify(const std::string& name, const std::string& pwd, bool isLogin){

}
int HttpRequest::ConverHex(char ch){
    if(ch >= 'A' && ch<='F')return ch-'A'+10;
    if(ch >= 'a' && ch<='f')return ch-'a'+10;
    return ch-'0';
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
