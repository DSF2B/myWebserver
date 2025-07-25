
#include <unistd.h>
#include "server/webserver.h"

int main() {
    WebServer server(
        1316, 3, 60000, false,             /* 端口 ET模式 timeoutMs 优雅退出  */
        3306, "root", "root", "yourdb", /* Mysql配置 */
        12, 6, true, 2, 1024, false);             /* 连接池数量 线程池数量 日志开关 日志等级 日志异步队列容量 */
    server.Start();
}
