 #include "log.h"

// 静态常量定义

Log::Log() : currentBuffer_(std::make_unique<Buffer>(kLargeBuffer)),
             nextBuffer_(std::make_unique<Buffer>(kLargeBuffer)) {
    fp_ = nullptr;
    isOpen_ = false;
    writeThread_ = nullptr;
    lineCount_ = 0;
    toDay_ = 0;
    isAsync_ = false;
    buffers_.reserve(16);
}
Log:: ~Log(){
    if(writeThread_ && writeThread_->joinable()) {
        running_ = false;
        condition_.notify_all();
        writeThread_->join();
    }
    if(fp_) {       // 冲洗文件缓冲区，关闭文件描述符
        std::lock_guard<std::mutex> locker(mutex_);
        flush();        // 清空缓冲区中的数据
        fclose(fp_);    // 关闭日志文件
    }
}
// 初始化日志实例（阻塞队列最大容量、日志保存路径、日志文件后缀）
void Log::init(int level=1, const char* path, const char* suffix, int maxQueCapacity) {
    isOpen_ = true;
    level_ = level;
    path_ = path;
    suffix_ = suffix;

    if(maxQueCapacity>0){
        isAsync_ = true;
        writeThread_ = std::make_unique<std::thread>(&Log::threadFunc, this);
    }else{
        isAsync_=false;
    }

    lineCount_=0;
    time_t timer = time(nullptr);
    struct tm* systime = localtime(&timer);
    //timer = mktime(ststime);
    char fileName[LOG_NAME_LEN]={0};
    //fprintf sprintf snprintf vsnprintf
    snprintf(fileName,LOG_NAME_LEN-1,"%s/%04d_%02d_%02d%s",path_,
        systime->tm_year+1900,systime->tm_mon+1,systime->tm_mday,suffix_);
    toDay_=systime->tm_mday;

    {
        std::lock_guard<std::mutex> locker(mutex_);
        if(fp_){// 重新打开
            flush();
            fclose(fp_);
        }
        fp_=fopen(fileName,"a");
        if(fp_ == nullptr){
            mkdir(path_,0777);
            fp_=fopen(fileName,"a");
        }
        assert(fp_!=nullptr);
    }
}


// 懒汉模式 c++11无需加锁 类外定义,不加static关键字
Log* Log::Instance(){
    static Log log;
    return &log;
}
// 异步写日志公有方法，调用私有方法asyncWrite
// void Log::FlushLogThread(){
//     Log::Instance()->AsyncWrite_();
// }
// 异步写日志方法,其他线程调用作为消费者
// void Log::AsyncWrite_(){
//     std::string str ="";
//     while(deque_->pop(str)){
//         std::lock_guard<std::mutex> locker(mtx_);
//         fputs(str.c_str(),fp_);
//     }
// }
// 将输出内容按照标准格式整理
void Log::write(int level, const char *format,...){
    //timeval timespec tm time_t
    struct timeval now;
    gettimeofday(&now,nullptr);
    // struct tm* sysTime = localtime(&now.tv_sec);//localtime ​​返回静态内存区的指针​​，多线程同时调用时返回值会被覆盖
    // struct tm t = *sysTime;//将 sysTime 指针指向的数据​​深拷贝到局部变量 t​​ 中
    struct tm t;
    localtime_r(&now.tv_sec, &t);
    va_list vaList;

    if(toDay_!=t.tm_mday || (lineCount_ &&(lineCount_%MAX_LINES ==0))){
        //如果不是今天，或日志行数超了，创建新的日志文件
        std::unique_lock<std::mutex> locker(mutex_);
        char newFileName[LOG_NAME_LEN]={0};
        if(toDay_!=t.tm_mday){
            //不是今天
            snprintf(newFileName,LOG_NAME_LEN-1,"%s/%04d_%02d_%02d%s",path_,
                t.tm_year+1900,t.tm_mon+1,t.tm_mday,suffix_);
            toDay_=t.tm_mday;
            lineCount_=0;
        }else{
            //日志行数超了，创建新的日志卷
            snprintf(newFileName,LOG_NAME_LEN-1,"%s/%04d_%02d_%02d-%d%s",path_,
                t.tm_year+1900,t.tm_mon+1,t.tm_mday,(lineCount_  / MAX_LINES),suffix_);
        }
        flush();
        fclose(fp_);
        fp_=fopen(newFileName,"a");
        assert(fp_!=nullptr);
    }
    char logLine[1024];
    int pos = 0;

    pos+=snprintf(logLine+pos,sizeof(logLine)-pos,"%d-%02d-%02d %02d%02d%02d.%06ld",
                t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec,now.tv_usec);
    AppendLogLevelTitle_(level,logLine,pos);

    va_start(vaList,format);
    int m=vsnprintf(logLine+pos,sizeof(logLine)-pos,format,vaList);
    va_end(vaList);
    logLine[pos++] = '\n';
    logLine[pos] = '\0';

    if(isAsync_) {
        std::lock_guard<std::mutex> lock(mutex_);
        lineCount_++;
        
        if(currentBuffer_->WritableBytes() > pos) {
            currentBuffer_->Append(logLine, pos);
        } else {
            buffers_.push_back(std::move(currentBuffer_));
            
            if(nextBuffer_) {
                currentBuffer_ = std::move(nextBuffer_);
            } else {
                currentBuffer_ = std::make_unique<Buffer>(kLargeBuffer);
            }
            currentBuffer_->Append(logLine, pos);
            condition_.notify_one();
        }
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        lineCount_++;
        fputs(logLine, fp_);
    }
}
void Log::threadFunc() {
    auto newBuffer1 = std::make_unique<Buffer>(kLargeBuffer);
    auto newBuffer2 = std::make_unique<Buffer>(kLargeBuffer);
    std::vector<std::unique_ptr<Buffer>> buffersToWrite;
    buffersToWrite.reserve(16);
    
    while(running_) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(buffers_.empty()) {
                condition_.wait_for(lock, std::chrono::seconds(3));
            }
            
            if(!buffers_.empty() || currentBuffer_->ReadableBytes() > 0) {
                buffers_.push_back(std::move(currentBuffer_));
                currentBuffer_ = std::move(newBuffer1);
                buffersToWrite.swap(buffers_);
                
                if(!nextBuffer_) {
                    nextBuffer_ = std::move(newBuffer2);
                }
            }
        }
        
        if(!buffersToWrite.empty()) {
            for(const auto& buffer : buffersToWrite) {
                fwrite(buffer->Peek(), 1, buffer->ReadableBytes(), fp_);
            }
            
            if(!newBuffer1) {
                newBuffer1 = std::move(buffersToWrite.back());
                buffersToWrite.pop_back();
                newBuffer1->RetrieveAll();
            }
            
            if(!newBuffer2) {
                newBuffer2 = std::move(buffersToWrite.back());
                buffersToWrite.pop_back();
                newBuffer2->RetrieveAll();
            }
            
            buffersToWrite.clear();
            fflush(fp_);
        }
    }
    
    // 处理剩余数据
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(currentBuffer_->ReadableBytes() > 0) {
            fwrite(currentBuffer_->Peek(), 1, currentBuffer_->ReadableBytes(), fp_);
        }
        for(const auto& buffer : buffers_) {
            fwrite(buffer->Peek(), 1, buffer->ReadableBytes(), fp_);
        }
    }
    fflush(fp_);
}


void Log::flush(){
    if(isAsync_){
        condition_.notify_all();
    }
    fflush(fp_);
}

int Log::GetLevel(){
    std::lock_guard<std::mutex> locker(mutex_);
    return level_;
}
void Log::SetLevel(int level){
    std::lock_guard<std::mutex> locker(mutex_);
    level_=level;
}
bool Log::IsOpen() { 
    return isOpen_; 
}
void Log::AppendLogLevelTitle_(int level, char* buffer, int& pos){
    const char* titles[] = {"[Debug]: ", "[Info] : ", "[Warn] : ", "[Error]: "};
    const char* title = (level >= 0 && level <= 3) ? titles[level] : titles[1];
    int len = strlen(title);
    memcpy(buffer + pos, title, len);
    pos += len;
}
