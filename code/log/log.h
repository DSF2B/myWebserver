#pragma once

#include <mutex> 
#include <string>
#include <thread>
#include <sys/time.h>
#include <string.h>
#include <stdarg.h>           // vastart va_end
#include <assert.h>
#include <sys/stat.h>         //mkdir
// #include "blockqueue.h"
#include <condition_variable>
#include <atomic>
#include <vector>
#include <memory>
#include "buffer.h"

const int LOG_PATH_LEN = 256;
const int LOG_NAME_LEN = 256;
const int MAX_LINES = 50000;
const int kLargeBuffer = 4000*1000;

class Log {
public:
    // 初始化日志实例（阻塞队列最大容量、日志保存路径、日志文件后缀）
    void init(int level, const char* path = "./log", 
                const char* suffix =".log",
                int maxQueueCapacity = 1024);

    static Log* Instance();
    // 异步写日志公有方法，调用私有方法asyncWrite
    // static void FlushLogThread();
    // 将输出内容按照标准格式整理
    void write(int level, const char *format,...);
    void flush();

    int GetLevel();
    void SetLevel(int level);
    bool IsOpen();
    // static Log& Instance(){
    //     return ptr;
    // }
    
private:
    Log();
    void AppendLogLevelTitle_(int level, char* buffer, int& pos);
    virtual ~Log();
    // 异步写日志方法,其他线程调用作为消费者
    // void AsyncWrite_();
    void threadFunc();

private:

    const char* path_;//路径名称
    const char* suffix_;//后缀名

    int MAX_LINES_; // 最大日志行数

    int lineCount_; //日志行数记录
    int toDay_;     //按当天日期区分文
    bool isOpen_;
     int level_;// 日志等级
    bool isAsync_;// 是否开启异步日志

    FILE* fp_; //打开log的文件指针

    std::unique_ptr<Buffer> currentBuffer_;
    std::unique_ptr<Buffer> nextBuffer_;
    std::vector<std::unique_ptr<Buffer>> buffers_;

    std::unique_ptr<std::thread> writeThread_; //写线程的指针
    std::condition_variable condition_;
    std::mutex mutex_;//同步日志必需的互斥量
    std::atomic<bool> running_{true};

    // static Log ptr;//饿汉模式
};
// Log* Log::ptr;

#define LOG_BASE(level, format, ...) \
    do {\
        Log* log = Log::Instance();\
        if (log->IsOpen() && log->GetLevel() <= level) {\
            log->write(level, format, ##__VA_ARGS__); \
        }\
    } while(0);

#define LOG_DEBUG(format, ...) do {LOG_BASE(0, format, ##__VA_ARGS__)} while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1, format, ##__VA_ARGS__)} while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2, format, ##__VA_ARGS__)} while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__)} while(0);

