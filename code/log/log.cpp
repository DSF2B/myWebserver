 #include "log.h"
Log::Log(){
    fp_ = nullptr;
    deque_ = nullptr;
    writeThread_ = nullptr;
    lineCount_ = 0;
    toDay_ = 0;
    isAsync_ = false;
}
Log:: ~Log(){
    if(writeThread_ && writeThread_->joinable()) {
        while(!deque_->empty()) {
            deque_->flush();    // 唤醒消费者，处理掉剩下的任务
        }

        deque_->Close();    // 关闭队列
        writeThread_->join();   // 等待当前线程完成手中的任务
    }
    if(fp_) {       // 冲洗文件缓冲区，关闭文件描述符
        std::lock_guard<std::mutex> locker(mtx_);
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
        isAsync_=true;
        if(!deque_){
            auto newQue = std::make_unique<BlockDeque<std::string>>();
            deque_= std::move(newQue);

            auto NewThread = std::make_unique<std::thread>(FlushLogThread);
            writeThread_ = std::move(NewThread);
        }
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
        std::lock_guard<std::mutex> locker(mtx_);
        // 取出所有数据，buffer归零，读写下标归零,
        buff_.RetrieveAll();
        //fopen fclose 
        //fprintf fscanf fputs fgets fwrite fread
        //fflush
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
void Log::FlushLogThread(){
    Log::Instance()->AsyncWrite_();
}
// 异步写日志方法,其他线程调用作为消费者
void Log::AsyncWrite_(){
    std::string str ="";
    while(deque_->pop(str)){
        std::lock_guard<std::mutex> locker(mtx_);
        fputs(str.c_str(),fp_);
    }
}
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
        std::unique_lock<std::mutex> locker(mtx_);
        locker.unlock();

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

        locker.lock();
        flush();
        fclose(fp_);
        fp_=fopen(newFileName,"a");
        assert(fp_!=nullptr);
    }
    
    {
        // 在buffer内生成一条对应的日志信息
        std::unique_lock<std::mutex> locker(mtx_);
        lineCount_++;
        int n=snprintf(buff_.BeginWrite(),128,"%d-%02d-%02d %02d%02d%02d.%06ld",
                t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec,now.tv_usec);
        buff_.HasWritten(n);
        AppendLogLevelTitle_(level);

        va_start(vaList,format);
        int m=vsnprintf(buff_.BeginWrite(),buff_.WritableBytes(),format,vaList);
        va_end(vaList);
        buff_.HasWritten(m);
        buff_.Append("\n\0",2);

        if(isAsync_ && deque_ && !deque_->full()){
            deque_->push_back(buff_.RetrieveAllToStr());
        }else{
            //fputs 只到\0
            fputs(buff_.Peek(),fp_);
        }
        buff_.RetrieveAll();
    }
}



void Log::flush(){
    if(isAsync_){
        deque_->flush();
    }
    fflush(fp_);
}

int Log::GetLevel(){
    std::lock_guard<std::mutex> locker(mtx_);
    return level_;
}
void Log::SetLevel(int level){
    std::lock_guard<std::mutex> locker(mtx_);
    level_=level;
}
bool Log::IsOpen() { 
    return isOpen_; 
}
void Log::AppendLogLevelTitle_(int level){
    switch(level){
        case 0:
        {
            buff_.Append("[Debug]: ",9);
            break;
        }
        case 1:
        {
            buff_.Append("[Info] : ",9);
            break;
        }
        case 2:
        {
            buff_.Append("[Warn] : ",9);
            break;
        }
        case 3:
        {
            buff_.Append("[Error]: ",9);
            break;
        }
        default:
        {
            buff_.Append("[Info] : ",9);
            break;
        }
    }
}

