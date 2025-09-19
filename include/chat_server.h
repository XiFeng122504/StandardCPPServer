#ifndef STANDARD_CPP_SERVER_CHAT_SERVER_H
#define STANDARD_CPP_SERVER_CHAT_SERVER_H

#include "thread_pool.h"

#include <netinet/in.h>
#include <sys/epoll.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

// ChatServer 封装了一个使用 epoll + 线程池的多人聊天室服务端。
//
// 设计目标：
// 1. 保持 main 函数简单，仅负责解析参数与启动服务。
// 2. 将 socket/epoll 的细节收敛在类内部，便于扩展新的功能。
// 3. 对外暴露少量接口，方便初学者理解生命周期。
class ChatServer {
public:
    struct Config {
        int port = 5555;                       // 监听端口
        int max_events = 64;                   // epoll 每次返回的最大事件数
        std::size_t read_buffer_size = 4096;   // 单次读取缓冲区大小
        std::size_t worker_threads = std::max<std::size_t>(2, std::thread::hardware_concurrency());
    };

    explicit ChatServer(Config config);
    ~ChatServer();

    // 启动服务器。返回 true 表示正常退出，false 表示初始化或运行过程中出现错误。
    bool run();

    // 允许在运行前自定义连接建立/断开/消息到达时的回调，方便日后扩展业务逻辑。
    void set_on_client_connected(std::function<void(int, const sockaddr_in&)> callback);
    void set_on_client_disconnected(std::function<void(int)> callback);
    void set_on_message(std::function<void(int, const std::string&)> callback);

private:
    bool prepare_listening_socket();
    bool register_listen_socket();
    bool event_loop();

    void handle_new_connection();
    void handle_client_event(int client_fd, uint32_t events);
    void close_client(int client_fd);

    void broadcast_message(const std::string& message, int sender_fd);

    Config config_;
    ThreadPool thread_pool_;

    int listen_fd_ = -1;
    int epoll_fd_ = -1;

    std::unordered_set<int> clients_;
    std::mutex clients_mutex_;

    std::function<void(int, const sockaddr_in&)> on_client_connected_;
    std::function<void(int)> on_client_disconnected_;
    std::function<void(int, const std::string&)> on_message_;

    std::atomic<bool> running_{false};
};

#endif  // STANDARD_CPP_SERVER_CHAT_SERVER_H
