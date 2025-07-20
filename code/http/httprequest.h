#pragma once

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>
#include <errno.h>     
#include <mysql/mysql.h>  //mysql

#include "buffer.h"
#include "log.h"
#include "sqlconnpool.h"
#include "sqlconnRAII.h"

class HttpRequest {
public:
    enum PARSE_STATE {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,        
    };

    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURSE,
        FORBIDDENT_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };
    
    HttpRequest();
    ~HttpRequest() = default;

    void Init();
    bool parse(Buffer& buff);
    std::string path() const;
    std::string& path();
    std::string method() const;
    std::string version() const;
    std::string GetHeader(const std::string& key) const;
    std::string GetHeader(const char* key) const;

    const std::string& body() const;
    bool IsKeepAlive() const;

private:
    bool ParseRequestLine_(const std::string& line);
    void ParseHeader_(const std::string& line);
    void ParseBody_(Buffer& buff);

    void ParsePath_();
    void ParsePost_();
    void ParseFromUrlencoded_();

    PARSE_STATE state_;
    std::string method_, path_, version_, body_;
    std::unordered_map<std::string, std::string> header_;

};
