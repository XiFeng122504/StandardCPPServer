#include "chat_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <vector>

namespace {
int set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }
    return 0;
}

void close_quietly(int fd) {
    if (fd >= 0) {
        ::close(fd);
    }
}
}  // namespace

ChatServer::ChatServer(Config config)
    : config_(config), thread_pool_(config.worker_threads) {}

ChatServer::~ChatServer() {
    running_.store(false);
    close_quietly(listen_fd_);
    close_quietly(epoll_fd_);
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (int client_fd : clients_) {
            close_quietly(client_fd);
        }
    }
}

bool ChatServer::run() {
    if (running_.load()) {
        return false;
    }

    if (!prepare_listening_socket()) {
        return false;
    }

    if (!register_listen_socket()) {
        return false;
    }

    running_.store(true);
    bool success = event_loop();
    running_.store(false);
    return success;
}

void ChatServer::set_on_client_connected(std::function<void(int, const sockaddr_in&)> callback) {
    on_client_connected_ = std::move(callback);
}

void ChatServer::set_on_client_disconnected(std::function<void(int)> callback) {
    on_client_disconnected_ = std::move(callback);
}

void ChatServer::set_on_message(std::function<void(int, const std::string&)> callback) {
    on_message_ = std::move(callback);
}

bool ChatServer::prepare_listening_socket() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::perror("socket");
        return false;
    }

    int enable = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        std::perror("setsockopt");
        return false;
    }

    if (set_non_blocking(listen_fd_) < 0) {
        std::perror("fcntl");
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(config_.port));

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        return false;
    }

    if (listen(listen_fd_, SOMAXCONN) < 0) {
        std::perror("listen");
        return false;
    }

    return true;
}

bool ChatServer::register_listen_socket() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        std::perror("epoll_create1");
        return false;
    }

    epoll_event event{};
    event.data.fd = listen_fd_;
    event.events = EPOLLIN;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) < 0) {
        std::perror("epoll_ctl: listen_fd");
        return false;
    }

    return true;
}

bool ChatServer::event_loop() {
    std::vector<epoll_event> events(static_cast<std::size_t>(config_.max_events));

    std::cout << "Chat server listening on port " << config_.port << std::endl;

    while (running_.load()) {
        int ready = epoll_wait(epoll_fd_, events.data(), config_.max_events, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("epoll_wait");
            return false;
        }

        for (int i = 0; i < ready; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listen_fd_) {
                handle_new_connection();
                continue;
            }

            handle_client_event(fd, ev);
        }
    }

    return true;
}

void ChatServer::handle_new_connection() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            std::perror("accept");
            break;
        }

        if (set_non_blocking(client_fd) < 0) {
            std::perror("fcntl: client");
            close_quietly(client_fd);
            continue;
        }

        epoll_event client_event{};
        client_event.data.fd = client_fd;
        client_event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &client_event) < 0) {
            std::perror("epoll_ctl: client");
            close_quietly(client_fd);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.insert(client_fd);
        }

        if (on_client_connected_) {
            on_client_connected_(client_fd, client_addr);
        } else {
            char addr_buffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, addr_buffer, sizeof(addr_buffer));
            std::cout << "New connection from " << addr_buffer << ":" << ntohs(client_addr.sin_port)
                      << " (fd=" << client_fd << ")" << std::endl;
        }
    }
}

void ChatServer::handle_client_event(int client_fd, uint32_t events) {
    if ((events & EPOLLERR) || (events & EPOLLHUP) || (events & EPOLLRDHUP)) {
        close_client(client_fd);
        return;
    }

    if (events & EPOLLIN) {
        std::string message;
        bool connection_closed = false;

        while (true) {
            std::vector<char> buffer(config_.read_buffer_size);
            ssize_t count = ::recv(client_fd, buffer.data(), buffer.size(), 0);
            if (count < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                std::perror("recv");
                connection_closed = true;
                break;
            }
            if (count == 0) {
                connection_closed = true;
                break;
            }

            message.append(buffer.data(), buffer.data() + count);

            if (static_cast<std::size_t>(count) < buffer.size()) {
                break;
            }
        }

        if (connection_closed) {
            close_client(client_fd);
            return;
        }

        if (!message.empty()) {
            if (on_message_) {
                on_message_(client_fd, message);
            }
            thread_pool_.enqueue([this, message, client_fd]() { broadcast_message(message, client_fd); });
        }
    }
}

void ChatServer::close_client(int client_fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    close_quietly(client_fd);

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(client_fd);
    }

    if (on_client_disconnected_) {
        on_client_disconnected_(client_fd);
    } else {
        std::cout << "Client disconnected: " << client_fd << std::endl;
    }
}

void ChatServer::broadcast_message(const std::string& message, int sender_fd) {
    std::vector<int> snapshot;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        snapshot.assign(clients_.begin(), clients_.end());
    }

    for (int client_fd : snapshot) {
        if (client_fd == sender_fd) {
            continue;
        }

        ssize_t total_sent = 0;
        while (total_sent < static_cast<ssize_t>(message.size())) {
            ssize_t sent = ::send(client_fd, message.data() + total_sent,
                                  message.size() - static_cast<std::size_t>(total_sent), MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                close_client(client_fd);
                break;
            }
            if (sent == 0) {
                close_client(client_fd);
                break;
            }
            total_sent += sent;
        }
    }
}
