#pragma once
#include "sqlconnpool.h"

/* 资源在对象构造初始化 资源在对象析构时释放*/
class SqlConnRAII {
public:
    SqlConnRAII(MYSQL** sql, SqlConnPool* connpool) {
        assert(sql);
        sql_=connpool->GetConn();
        *sql = sql_;
        connpool_=connpool;
    }
    ~SqlConnRAII() {
        if(sql_){
            connpool_->FreeConn(sql_);
        }
    }
private:
    MYSQL* sql_;
    SqlConnPool* connpool_;
};
