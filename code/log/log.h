/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#ifndef LOG_H
#define LOG_H

#include <mutex> 
#include <string>
#include <thread>
#include <sys/time.h>
#include <string.h>
#include <stdarg.h>           // vastart va_end
#include <assert.h>
#include <sys/stat.h>         //mkdir
#include "blockqueue.h"
#include "../buffer/buffer.h"

class Log {
public:
    // 初始化日志实例（阻塞队列最大容量、日志保存路径、日志文件后缀）
    void init(int level, const char* path = "./log", 
                const char* suffix =".log",
                int maxQueueCapacity = 1024);

    static Log* Instance();
    // 异步写日志公有方法，调用私有方法asyncWrite
    static void FlushLogThread();
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
    void AppendLogLevelTitle_(int level);
    virtual ~Log();
    // 异步写日志方法,其他线程调用作为消费者
    void AsyncWrite_();

private:
    static const int LOG_PATH_LEN = 256;    // 日志文件最长文件名
    static const int LOG_NAME_LEN = 256;    // 日志最长名字
    static const int MAX_LINES = 50000;     // 日志文件内的最长日志条数

    const char* path_;//路径名称
    const char* suffix_;//后缀名

    int MAX_LINES_; // 最大日志行数

    int lineCount_; //日志行数记录
    int toDay_;     //按当天日期区分文件

    bool isOpen_;
 
    Buffer buff_;// 输出的内容，缓冲区
    int level_;// 日志等级
    bool isAsync_;// 是否开启异步日志


    FILE* fp_; //打开log的文件指针
    std::unique_ptr<BlockDeque<std::string>> deque_; //阻塞队列
    std::unique_ptr<std::thread> writeThread_; //写线程的指针
    std::mutex mtx_;//同步日志必需的互斥量

    // static Log ptr;//饿汉模式
};
// Log* Log::ptr;

#define LOG_BASE(level, format, ...) \
    do {\
        Log* log = Log::Instance();\
        if (log->IsOpen() && log->GetLevel() <= level) {\
            log->write(level, format, ##__VA_ARGS__); \
            log->flush();\
        }\
    } while(0);

#define LOG_DEBUG(format, ...) do {LOG_BASE(0, format, ##__VA_ARGS__)} while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1, format, ##__VA_ARGS__)} while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2, format, ##__VA_ARGS__)} while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__)} while(0);

#endif //LOG_H