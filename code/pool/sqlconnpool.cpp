#include "sqlconnpool.h"

SqlConnPool::SqlConnPool(){
    int useCount_=0;
    int freeCount_=0;
}
SqlConnPool::~SqlConnPool(){
    ClosePool();
}
SqlConnPool* SqlConnPool::Instance(){
    static SqlConnPool connPool;
    return &connPool;
}
void SqlConnPool::Init(const char* host, int port,
const char* user,const char* pwd, 
const char* dbName, int connSize){
    for(int i=0;i<connSize;i++){
        MYSQL *sql = nullptr;
        sql = mysql_init(sql);
        if(!sql){
            LOG_ERROR("MYSQL init error");
            assert(sql);
        }
        sql = mysql_real_connect(sql, host, user, pwd, dbName, port, nullptr, 0);
        if (!sql) {
            LOG_ERROR("MYSQL connect error");
        }
        connQue_.emplace(sql);
    }
    MAX_CONN_=connSize;
    sem_init(&semId_,0,MAX_CONN_);
}

MYSQL* SqlConnPool::GetConn(){
    MYSQL* conn=nullptr;
    if(connQue_.empty()){
        LOG_ERROR("SqlConnPool busy");
        return nullptr;
    }
    sem_wait(&semId_);
    std::lock_guard<std::mutex> locker(mtx_);
    conn=connQue_.front();
    connQue_.pop();
    return conn;
}
void SqlConnPool::FreeConn(MYSQL * conn){
    assert(conn);
    std::lock_guard<std::mutex> locker(mtx_);
    connQue_.push(conn);
    sem_post(&semId_);
}
int SqlConnPool::GetFreeConnCount(){
    std::lock_guard<std::mutex> locker(mtx_);
    return connQue_.size();
}
void SqlConnPool::ClosePool(){
    std::lock_guard<std::mutex> locker(mtx_);
    while(!connQue_.empty()){
        auto conn=connQue_.front();
        connQue_.pop();
        mysql_close(conn);
    }
    mysql_library_end();
}
