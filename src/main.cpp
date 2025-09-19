#include "chat_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <csignal>
#include <iostream>

// 主函数只负责：
// 1. 捕获 SIGPIPE，防止写入已关闭的 socket 时程序直接退出。
// 2. 解析命令行参数（如果给定）来指定端口。
// 3. 创建 ChatServer 对象并运行。
int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);

    ChatServer::Config config;
    if (argc > 1) {
        config.port = std::stoi(argv[1]);
    }

    ChatServer server(config);

    // 初学者可以在这里配置自定义的回调，以便打印更详细的日志，
    // 或者接入数据库、业务逻辑等。
    server.set_on_client_connected([](int fd, const sockaddr_in& addr) {
        char buffer[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &addr.sin_addr, buffer, sizeof(buffer));
        std::cout << "[INFO] 新客户端连接: " << buffer << ":" << ntohs(addr.sin_port)
                  << " (fd=" << fd << ")" << std::endl;
    });

    server.set_on_client_disconnected([](int fd) {
        std::cout << "[INFO] 客户端断开: fd=" << fd << std::endl;
    });

    server.set_on_message([](int fd, const std::string& message) {
        std::cout << "[INFO] 收到来自 fd=" << fd << " 的消息: " << message << std::endl;
    });

    if (!server.run()) {
        std::cerr << "服务器启动失败，请检查上方日志" << std::endl;
        return 1;
    }

    return 0;
}
