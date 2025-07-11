#pragma once

#include <queue>
#include <unordered_map>
#include <time.h>
#include <algorithm>
#include <arpa/inet.h> 
#include <functional> 
#include <assert.h> 
#include <chrono>
#include <mutex>
#include "log.h"

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode {
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;
    bool operator<(const TimerNode& t){
        return expires<t.expires;
    }
    bool operator>(const TimerNode& t){
        return expires>t.expires;
    }
};
class HeapTimer {
public:
    HeapTimer();
    ~HeapTimer();
    void adjust(int id, int newExpires);
    void add(int id, int timeOut, const TimeoutCallBack& cb);
    void clear();
    void tick();
    void pop();
    int GetNextTick();
private:
    void del_(size_t i);
    void siftup_(size_t i);
    bool siftdown_(size_t index, size_t n);
    void SwapNode_(size_t i, size_t j);

    std::vector<TimerNode> heap_;
    // id对应的在heap_中的下标，方便用heap_的时候查找
    std::unordered_map<int, size_t> ref_;//fd idx
    std::mutex mtx_;
    std::vector<TimeoutCallBack> callbacks_;

};
