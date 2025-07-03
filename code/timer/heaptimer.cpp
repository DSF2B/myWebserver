#include "heaptimer.h"

HeapTimer::HeapTimer() { 
    heap_.reserve(64);//只分配内存，不构造元素
}
HeapTimer::~HeapTimer() {
     clear(); 
}
void HeapTimer::adjust(int id, int newExpires){
    auto it =ref_.find(id);
    assert(!heap_.empty() && it!=ref_.end());
    heap_[it->second].expires = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(newExpires);
    siftdown_(it->second,heap_.size());    
}
void HeapTimer::add(int id, int timeOut, const TimeoutCallBack& cb){
    assert(id>=0);
    auto it=ref_.find(id);
    if(it!=ref_.end()){
        int idx = it->second;
        heap_[idx].expires = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(timeOut);
        heap_[idx].cb=cb;
        if(!siftdown_(idx,heap_.size())){
            siftup_(idx);
        }
    }else{
        
        heap_.push_back({id,std::chrono::high_resolution_clock::now()+std::chrono::microseconds(timeOut),cb});
        ref_[id]=heap_.size()-1;
        siftup_(heap_.size()-1);
    }
}
// 删除指定id，并触发回调函数
void HeapTimer::doWork(int id){
    auto it=ref_.find(id);
    if(heap_.empty() || it==ref_.end()){
        return ;
    }
    auto node=heap_[it->second];
    node.cb();
    del_(it->second);    
}

void HeapTimer::tick(){
    if(heap_.empty()){
        return ;
    }
    while(!heap_.empty()){
        auto node=heap_.front();
        if(std::chrono::duration_cast<std::chrono::microseconds>(node.expires-std::chrono::high_resolution_clock::now()).count()>0){
            //超时时间未到
            break;
        }
        node.cb();
        HeapTimer::pop();
    }
}
void HeapTimer::pop(){
    assert(!heap_.empty());
    del_(0);
}
//返回下一个超时时间间隔
int HeapTimer::GetNextTick(){
    tick();
    int res_timeout_sec=-1;
    if(!heap_.empty()){
        res_timeout_sec=std::chrono::duration_cast<std::chrono::milliseconds>(heap_.front().expires - std::chrono::high_resolution_clock::now()).count();
        if(res_timeout_sec<0)return 0;
    }
    return res_timeout_sec;

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
    ref_.clear();
    heap_.clear();
}