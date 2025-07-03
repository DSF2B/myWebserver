#include "epoller.h"
/*
int epoll_create(int size);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);

struct epoll_event {
    uint32_t events; // Epoll events 
    epoll_data_t data; // User data variable 
};
typedef union epoll_data {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;
*/


Epoller::Epoller(int maxEvent = 1024):epollFd_(epoll_create(512)),events_(maxEvent){
    assert(epollFd_>=0 && events_.size()>0);
}

Epoller::~Epoller(){
    close(epollFd_);
}

bool Epoller::AddFd(int fd, uint32_t events){
    if(fd<0)return false;
    epoll_event ev={0};
    ev.data.fd=fd;
    ev.events=events;
    return 0==epoll_ctl(epollFd_,EPOLL_CTL_ADD,fd,&ev);
}

bool Epoller::ModFd(int fd, uint32_t events){
    if(fd<0)return false;
    epoll_event ev={0};
    ev.data.fd=fd;
    ev.events=events;
    return 0==epoll_ctl(epollFd_,EPOLL_CTL_MOD,fd,&ev);
}

bool Epoller::DelFd(int fd){
    if(fd<0)return false;
    return 0==epoll_ctl(epollFd_,EPOLL_CTL_DEL,fd,0);
}

int Epoller::Wait(int timeoutMs = -1){
    return epoll_wait(epollFd_, &events_[0],static_cast<int>(events_.size()),timeoutMs);
}

int Epoller::GetEventFd(size_t i) const{
    assert(i>=0 && i<events_.size());
    return events_[i].data.fd;
}
uint32_t Epoller::GetEvents(size_t i) const{
    assert(i>=0 && i<events_.size());
    return events_[i].events;
}
