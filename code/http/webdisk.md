## total
应用层以下完成监听、接收、发送、解析hhtp请求、制作http响应，应用层完成请求体解析，响应体设置。为了满足大文件传输需求，采用sendFile+writev结合的方式，发送静态网页和文件时使用sendFile,发送json响应时使用writev，减少系统调用次数。

Content-Type：注册和登录使用application/x-www-form-urlencoded，**URL编码表单数据**。传输文件使用multipart/form-data，**表单数据**。

解析好的请求发送到应用层，进一步解析请求行、请求体。提取请求行中的路径，请求路径分为静态网页和功能路由，应用层根据请求路径完成不同功能。解析请求行具体内容，URL表单提取用户名和密码，表单数据完成分割，边界符号在content-type中附带。
#### readv经常读不到完整的数据
**采用长度验证：读取到数据后，验证请求体长度是否正确，否则要求重传。**
## 注册和登录 
注册和登录功能主要完成数据库操作、创建目录、生成回话令牌和网页跳转。数据库操作就是插入和查询，使用从线程池拿一个连接完成sql操作。

## Token
webdisk网页的任何请求都携带token，登录时在服务器端注册token，每次操作验证token。**token=std::to_string(time(nullptr)) + "_" + std::to_string(rand())**

回话令牌使用单例模式，增删查改加锁操作。复习一下懒汉模式静态局部变量实现的单例模式：
    
    static SessionManager& getInstance(){
        static SessionManager instance;
        return instance;
    }


## 页面展示
使用json响应，在登录和刷新后发送listfile请求，服务器找到用户对应的目录，填写json发送到浏览器。json响应使用application/json。json使用键值对：

    {
        "name": "Alice",
        "age": 30,
        "isStudent": false,
        "courses": ["Math", "CS"],
        "contact": {
            "email": "alice@example.com",
            "phone": null
        },
        "metadata": {
            "createdAt": "2025-01-01T12:00:00Z"
        }
    }
## 上传
上传由于需要加载到缓冲区解析，因此使用分块上传，前端将文件进行切分依次上传，最后上传合并信号。每次上传使用formData附带其他信息，如临时文件名fileId、分块索引，文件名称等，服务器将分块数据保存在chunk临时文件夹中。合并时利用ofstream和ifstream把临时文件夹的内容进行合并。

由于内核缓冲区大小限制，经常接收不到完整数据，采用长度验证，前端重传机制。
## 下载和删除
下载就是提取路径，完成对应操作即可。

## 文件操作
使用了std::filesystem的一些方法如directory_iterator、is_regular_file、create_directories、rename、remove等方法。

#### todo: 分块下载
#### todo：logout删除token和定时更新