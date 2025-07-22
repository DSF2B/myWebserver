#include "heaptimer.h"

HeapTimer::HeapTimer() { 
    heap_.reserve(1024);//只分配内存，不构造元素
}
HeapTimer::~HeapTimer() {
     clear(); 
}

void HeapTimer::adjust(int id, int newExpires){
    std::lock_guard<std::mutex> lock(mtx_);
    if(ref_.count(id) == 0){
        std::cout<<id;
        return;
    }
    assert(!heap_.empty() && ref_.count(id)>0);
    int idx=ref_[id];
    heap_[idx].expires = std::chrono::high_resolution_clock::now() + MS(newExpires);
    siftdown_(idx,heap_.size());    
}
void HeapTimer::add(int id, int timeOut, const TimeoutCallBack& cb){
    std::lock_guard<std::mutex> lock(mtx_);
    assert(id>=0);
    auto it=ref_.find(id);
    if(it!=ref_.end()){
        int idx = it->second;
        heap_[idx].expires = std::chrono::high_resolution_clock::now() + MS(timeOut);
        heap_[idx].cb=cb;
        if(!siftdown_(idx,heap_.size())){
            siftup_(idx);
        }
    }else{
        heap_.push_back({id,std::chrono::high_resolution_clock::now()+MS(timeOut),cb});
        ref_[id]=heap_.size()-1;
        siftup_(heap_.size()-1);
    }
}

// void HeapTimer::tick(){
//     if(heap_.empty()){
//         return ;
//     }
//     while(!heap_.empty()){
//         auto node=heap_.front();
//         if(std::chrono::duration_cast<std::chrono::microseconds>(node.expires-std::chrono::high_resolution_clock::now()).count()>0){
//             //超时时间未到
//             break;
//         }
//         node.cb();
//         HeapTimer::pop();
//     }
// }
//返回下一个超时时间间隔
// int HeapTimer::GetNextTick(){
//     std::lock_guard<std::mutex> lock(mtx_);
//     tick();
//     int res_timeout_sec=100;
//     if(!heap_.empty()){
//         res_timeout_sec=std::chrono::duration_cast<MS>(heap_.front().expires - std::chrono::high_resolution_clock::now()).count();
//         if(res_timeout_sec<0)return 0;
//     }
//     return res_timeout_sec;

// }
int HeapTimer::GetNextTick() {
    std::unique_lock<std::mutex> lock(mtx_);
    // 处理超时事件（不执行回调）
    while (!heap_.empty()) {
        auto& node = heap_.front();
        auto now = Clock::now();
        if (node.expires > now) break;
        // 提取回调但不执行（避免在锁内执行）
        callbacks_.push_back(std::move(node.cb));
        pop(); // 内部无锁版本
    }
    // 计算下次超时时间
    int next_timeout = -1;
    if (!heap_.empty()) {
        auto duration = heap_.front().expires - Clock::now();
        next_timeout = std::chrono::duration_cast<MS>(duration).count();
        next_timeout = std::max(next_timeout, 0);
    }
    lock.unlock(); // 提前释放锁
    // 在锁外执行回调
    for (auto& cb : callbacks_) {
        if (cb) cb();
    }
    callbacks_.clear();
    return next_timeout;
}
void HeapTimer::del_(size_t i){
    assert(i>=0 && i<heap_.size());
    size_t tmp=i;
    size_t n=heap_.size()-1;
    assert(tmp<=n);
    // 如果就在队尾，就不用移动了
    if(i < n){
        SwapNode_(tmp,heap_.size()-1);
        if(!siftdown_(tmp,n)){
            siftup_(tmp);
        }
    }
    ref_.erase(heap_.back().id);
    heap_.pop_back();
}
void HeapTimer::del_fd(int fd){
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = ref_.find(fd);
    if (it == ref_.end()) return ;
    size_t i = it->second;
    del_(i);
    return;
}
void HeapTimer::siftup_(size_t i){
    assert(i>=0 && i<heap_.size());
    size_t parent = (i-1)/2;
    while(parent>=0){
        if(heap_[parent]>heap_[i]){
            SwapNode_(i,parent);
            i=parent;
            parent=(i-1)/2;
        }else{
            break;
        }
    }
}
void HeapTimer::pop(){
    assert(!heap_.empty());
    del_(0);
}
// false：不需要下滑  true：下滑成功
bool HeapTimer::siftdown_(size_t i, size_t n){
    assert(i >= 0 && i < heap_.size());
    assert(n >= 0 && n <= heap_.size());
    auto index=i;
    auto child=2*index+1;
    while(child<n){
        if(child+1<n && heap_[child+1]<heap_[child]){
            child++;
        }
        if(heap_[child]<heap_[index]){
            SwapNode_(child,index);
            index=child;
            child=2*child+1;
        }else{
            break;
        }
    }
    return index>i;
}
void HeapTimer::SwapNode_(size_t i, size_t j){
    assert(i>=0 && i<heap_.size());
    assert(j>=0 && j<heap_.size());
    std::swap(heap_[i],heap_[j]);
    ref_[heap_[i].id]=i;
    ref_[heap_[j].id]=j;
}
void HeapTimer::clear(){
    std::lock_guard<std::mutex> lock(mtx_);
    ref_.clear();
    heap_.clear();
}
