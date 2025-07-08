#pragma once

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <functional>
#include <assert.h>

class ThreadPool {
public:
    ThreadPool()=default;
    ThreadPool(ThreadPool&&)=default;
    ~ThreadPool(){
        if(static_cast<bool>(pool_)) {
            {
                std::lock_guard<std::mutex> locker(pool_->mtx);
                pool_->isClosed = true;
            }
            pool_->cond.notify_all();
        }
    }
    // 尽量用make_shared代替new，如果通过new再传递给shared_ptr，内存是不连续的，会造成内存碎片化
    // explicit 修饰构造函数时，​​禁止编译器执行隐式类型转换​​，确保对象只能通过显式调用构造
    explicit ThreadPool(int threadCount = 8): pool_(std::make_shared<Pool>()){
        assert(threadCount>0);
        for(int i=0;i<threadCount;i++){
            //​​不需要后续操作​,​​匿名对象写法​​
            std::thread([pool = pool_](){
                std::unique_lock<std::mutex> locker(pool->mtx);
                while(true){
                    if(!pool->tasks.empty()){
                        auto task = std::move(pool->tasks.front());
                        pool->tasks.pop();
                        locker.unlock();
                        task();
                        locker.lock(); // 马上又要取任务了
                    }else if(pool->isClosed){
                        break;
                    }else{
                        pool->cond.wait(locker);
                    }
                }
            }).detach();
        }
    }
    
    template<typename T>
    void AddTask(T&& task){
        //T&& 在模板类型推导上下文中称为​​万能引用​​（也称为转发引用）。
        //它既能绑定到左值（如具名变量），也能绑定到右值（如临时对象或 std::move 的结果）
        // T&& & → T& （依旧左值引用）
        // T&& && → T&& （依旧右值引用）
        std::unique_lock<std::mutex> locker(pool_->mtx);
        //std::forward主要用于完美转发，保留传递给函数参数的值类别（lvalue 或 rvalue）
        //在编写泛型代码时非常有用，尤其是在模板函数中。
        pool_->tasks.emplace(std::forward<T>(task));
        pool_->cond.notify_one();
    }
private:
    struct Pool{
        std::mutex mtx;
        std::condition_variable cond;
        bool isClosed;
        std::queue<std::function<void()>> tasks;// 任务队列，函数类型为void()
    };
    std::shared_ptr<Pool> pool_;
};
