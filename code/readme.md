# Total 

尽量用make_shared代替new去配合shared_ptr使用，因为如果通过new再传递给shared_ptr，内存是不连续的，会造成内存碎片化

    std::chrono::high_resolution_clock
    duration（一段时间）
    time_point（时间点）
一般获取时间点是通过clock时钟获得的，一共有3个：
    ①、high_resolution_clock
    ②、system_clock
    ③、steady_clock

* high_resolution_clock
他有一个now()方法，可以获取当前时间。注意：std::chrono::high_resolution_clock返回的时间点是按秒为单位的。

* std::chrono::milliseconds表示毫秒，可用于duration<>的模板类，举例：chrono::duration_cast<milliseconds>

# buffer
即每次send()不一定会发送完，没发完的数据要用一个容器进行接收，所以必须要实现应用层缓冲区。

vectorh缓冲区，设置原子类型的读写索引，对外借口为读取或写入某个fd。

从某个连接接受数据的时候，有可能会超过vector的容量，所以我们用readv()来分散接受来的数据。

在非阻塞网络编程中，如何设计并使用缓冲区？一方面我们希望减少系统调用，一次读的数据越多越划算，那么似乎应该准备一个大的缓冲区。另一方面，我们系统减少内存占用。如果有 10k 个连接，每个连接一建立就分配 64k 的读缓冲的话，将占用 640M 内存，而大多数时候这些缓冲区的使用率很低。muduo 用 readv 结合栈上空间巧妙地解决了这个问题。

在栈上申请一个65536字节的stackbuf，利用readv读取数据，iov有两块，第一块指向 muduo Buffer 中的 writable 字节，另一块指向栈上的 stackbuf。这样如果读入的数据不多，那么全部都读到 Buffer 中去了；如果长度超过 Buffer 的 writable 字节数，就会读到栈上的 stackbuf 里，然后程序再把 stackbuf 里的数据 append 到 Buffer 中。

利用临时的栈上空间，避免每个连接开辟巨大buffere造成的内存浪费，也避免u反复调用read()的系统调用开销

# log
同步日志：日志写入函数与工作线程串行执行，由于涉及到I/O操作，当单条日志比较大的时候，同步模式会阻塞整个处理流程，服务器所能处理的并发能力将有所下降，尤其是在峰值的时候，写日志可能成为系统的瓶颈。

异步日志：将所写的日志内容先存入阻塞队列中，写线程从阻塞队列中取出内容，写入日志

阻塞队列使用锁和条件变量，push时如果没有空余空间，就等待在生产者条件变量，pop时如果没有元素就等待在消费者条件变量

日志采用单例模式，分为懒汉模式和饿汉模式。
懒汉模式： 顾名思义，非常的懒，只有当调用getInstance的时候，才会去初始化这个单例。其中在C++11后，不需要加锁，直接使用函数内局部静态对象即可。

    class TaskQueue {
    public:
        static TaskQueue* getInstance() {
            if (m_taskQ == nullptr) {
                lock();
                if (m_taskQ == nullptr) {
                    m_taskQ = new TaskQueue();
                }
                unlock();
            }
            return m_taskQ;
        }
    private:
        TaskQueue() = default;
        TaskQueue(const TaskQueue&);
        TaskQueue& operator=(const TaskQueue&);
        static TaskQueue* m_taskQ;
    };
静态局部变量实现的懒汉模式:

    class TaskQueue {
    public:
        static TaskQueue* getInstance() {
            lock();
            static TaskQueue instance;
            unlock();
            return &instance;
        }
    private:
        TaskQueue() = default;
        TaskQueue(const TaskQueue&);
        TaskQueue& operator=(const TaskQueue&);
    };
饿汉模式： 即迫不及待，在程序运行时立即初始化。饿汉模式不需要加锁，就可以实现线程安全，原因在于，在程序运行时就定义了对象，并对其初始化。之后，不管哪个线程调用成员函数getinstance()，都只不过是返回一个对象的指针而已。所以是线程安全的，不需要在获取实例的成员函数中加锁。

    class TaskQueue {
    public:
        static TaskQueue*(){
            return taskQ_;
        }
    private:
        TaskQueue() = default;//私有化构造函数\拷贝构造函数\赋值运算符
        TaskQueue(const TaskQueue&);
        TaskQueue& operator=(const TaskQueue&);
        static TaskQueue* taskQ_;   
    };
TaskQueue* TaskQueue::taskQ_= new TaskQueue//类外定义并初始化静态成员变量，这句代码在程序启动时就执行，立即创建了 TaskQueue 的唯一实例

日志函数使用宏和可变参数列表实现

    log->write(level, format, ##__VA_ARGS__);
    va_list vaList;
    va_start(vaList, format);
    int m = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, vaList);
    va_end(vaList);

# ConnPool
连接池采用生产者消费者模型，采用锁+信号量，预先产生多条连接std::queue<MYSQL *>。

http释放时把连接释放回连接池，先加锁锁定内存空间，然后push，然后sem_post增加信号量。

http取连接时从连接池消费连接。等待在信号量上，当信号量大于0时加锁继续取连接。

连接池采用RAII思想，获取资源即初始化，避免资源泄露。智能指针就是经典的RAII。

    class SqlConnRAII {
    public:
        SqlConnRAII(MYSQL** sql, SqlConnPool* connpool) {
            assert(sql);
            sql_=connpool->GetConn();
            *sql = sql_;
            connpool_=connpool;
        }
        ~SqlConnRAII() {
            if(sql_){
                connpool_->FreeConn(sql_);
            }
        }
    private:
        MYSQL* sql_;
        SqlConnPool* connpool_;
    };
当需要创建或得到sql*时：

    MYSQL *sql;
    SqlConnRAII(&sql,SqlConnPool::Instance());

# ThreadPool
工作队列和线程池分别为生产者消费者，生产者队列用std::queue<std::function<void()>>,线程在初始化线程池时创建匿名线程，回调函数为死循环取任务，注意：被唤醒后判断任务是否已经被取走\线程池是否已经关闭。执行回调函数前需要释放锁，执行完毕后再拿锁。

向工作队列添加任务函数采用模板，使用万能引用+完美转发，开销最小：无论是左值还是右值都是不会拷贝：左值（引用），右值（移动）
若使用值传递，左值会触发拷贝构造函数。若使用万能引用，左值传递引用，无需拷贝，右值触发移动语义。

    template<typename T>
    void wrapper(T&& arg) {
        target(std::forward<T>(arg));  // 根据原始类型调用正确重载
    }
根据引用折叠，若传入​​左值​​（如 int a），T 推导为 int&，形参类型折叠为 int&（左值引用）。若传入​​右值​​（如 42），T 推导为 int，形参类型为 int&&（右值引用）。
即使形参是右值引用，在函数内部具名变量 arg 仍是左值（因其有名称和持久地址）。采用forward能恢复参数的原始值类别（左值/右值）根据 T 的推导结果，std::forward<T>(arg) 返回：
    若 T 是左值引用（如 int&），返回左值引用。
    若 T 是普通类型（如 int），返回右值引用。


信号量和条件变量
​​信号量更适用​​
* 控制固定数量的资源（如数据库连接池限流）
* 简单同步场景（如仅需限制并发线程数）

​​条件变量更适用​​
* 条件复杂的等待（如缓冲区有数据且未被锁定）
* 需广播通知所有等待线程（如线程池任务分配）

条件变量通常更轻量，但需配合锁；信号量涉及系统调用，开销较大
    //消费
    sem_wait(&semId_);
    std::lock_guard<std::mutex> locker(mtx_);
    connQue_.pop();
    //生产
    std::lock_guard<std::mutex> locker(mtx_);
    connQue_.push(conn);
    sem_post(&semId_);

# timer

网络编程中除了处理IO事件之外，定时事件也同样不可或缺，如定期检测一个客户连接的活动状态、游戏中的技能冷却倒计时以及其他需要使用超时机制的功能。我们的服务器程序中往往需要处理众多的定时事件，因此有效的组织定时事件，使之能在预期时间内被触发且不影响服务器主要逻辑，对我们的服务器性能影响特别大。

我们的web服务器也需要这样一个时间堆，定时剔除掉长时间不动的空闲用户，避免他们占着茅坑不拉屎，耗费服务器资源。

一般的做法是将每个定时事件封装成定时器，并使用某种容器类数据结构将所有的定时器保存好，实现对定时事件的统一管理。常用方法有排序链表、红黑树、时间堆和时间轮。这里使用的是时间堆。

时间堆的底层实现是由小根堆实现的。小根堆可以保证堆顶元素为最小的。

堆包括两个主要成员：ref和heap，ref存储fd与其在heap中的位置，实现O(1)查找。

堆有两个基本操作，up和down
* up
    自尾节点和父节点比较，移动到父节点
* down
    和两个孩子比较，与最小的孩子交换，移动到这个孩子

定时器堆对外提供adjust、add和GetNextTick
* adjust(int id, int newExpires)
    adjust将指定id的timer设置为新的超时时间，对新的timer执行down函数。
* add(int id, int timeOut, const TimeoutCallBack& cb)
    如果有这个timer存在，就设置为新的超时时间；如果不存在，新建一个timer，放到heap最后，执行up函数。
* GetNextTick()
    tick一下，pop出所有超时的timer，将这些超时timer的回调函数记录下来，计算下次超时时间为顶部timer的超时时间，释放锁。执行所有回调函数关闭连接。
    ### 主Reactor会添加timer，从Reactor会调整timer，因此需要加锁。

    ### 设置超时时间为60s，60s后这里的超时时间非常短，超时触发太频繁，可以容忍一些连接未关闭。

# http

# server

# 遇到的问题