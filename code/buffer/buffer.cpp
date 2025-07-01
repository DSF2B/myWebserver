#include "buffer.h"

Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0)  {}  

size_t Buffer::WritableBytes() const{
    return buffer_.size() - writePos_;
} 
size_t Buffer::ReadableBytes() const{
    return writePos_-readPos_;
}
size_t Buffer::PrependableBytes() const{
    return readPos_;
}

const char* Buffer::Peek() const{
    return &buffer_[readPos_];
}
// 确保可写的长度
void Buffer::EnsureWriteable(size_t len){
    if(len>WritableBytes()){
        MakeSpace_(len);
    }
    assert(len <= WritableBytes());
}
// 移动写下标，在Append中使用
void Buffer::HasWritten(size_t len){
    writePos_ += len;
}
// 读取len长度，移动读下标
void Buffer::Retrieve(size_t len){
    readPos_ += len;
}
// 读取到end位置
void Buffer::RetrieveUntil(const char* end){
    assert(Peek() <= end);
    Retrieve(end - Peek());
}
// 取出所有数据，buffer归零，读写下标归零,在别的函数中会用到
void Buffer::RetrieveAll(){
    bzero(&buffer[0],buffer_.size());
    readPos_ = writePos_=0;
}
// 取出剩余可读的str
std::string Buffer::RetrieveAllToStr(){
    std::string str(Peek(),ReadableBytes());
    RetrieveAll();
    return str;
}

const char* Buffer::BeginWriteConst() const{
    return &buffer[writePos_];
}
char* Buffer::BeginWrite(){
    return &buffer[writePos_];
}
void Buffer::Append(const char* str, size_t len){
    assert(str);
    EnsureWriteable(len);
    std::copy(str,str+len,BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const std::string& str){
    Append(str,str.size());
}
void Buffer::Append(const void* data, size_t len){
    Append(static_cast<const,char*>(data),len);
}
void Buffer::Append(const Buffer& buff){
    Append(buff.Peek(),buff.ReadableBytes());
}
ssize_t Buffer::ReadFd(int fd, int* Errno){
    char buff[65535];
    struct iovec iov[2];
    size_t writeable=WriteableBytes();//目前剩余空间
    iov[0].iov_base=BeginWrite();
    iov[0].iov_len=writeable;
    iov[1].iov_base = buff;
    iov[1].iov_len=sizeof(buff);

    const int iovcnt=(writeable < sizeof(buff)? 2:1);
    ssize_t len=readv(fd,&iov,iovcnt);
    if(len<0){
        *Errno = errno;
    }else if(static_cast<size_t>(len)<=writeable){
        HasWritten(len);
    }else{
        writePos_ = buffer_.size();
        Append(buff,static_cast<size_t> (len-writeable));
    }
    return len;

}
ssize_t Buffer::WriteFd(int fd, int* Errno){
    ssize_t len=write(fd,Peek(),ReadableBytes());
    if(len<0){
        *Errno = errno;
        return len;
    }
    Retrieve(len);
    return len;
}

char* Buffer::BeginPtr_(){
    return &buffer[0];
}
const char* Buffer::BeginPtr_() const{
    return &buffer[0];
}
void Buffer::MakeSpace_(size_t len){
    if(WriteableBytes() + PrependableBytes() < len){
        buffer.resize(writePos_+len+1);
    }else{
        size_t readable=ReadableBytes();
        std::copy(BeginPtr_() + readPos_,BeginPtr_()+writePos_,BeginPtr_());
        readPos_=0;
        writePos_=readable;
        assert(readable == ReadableBytes(););
    }
}
