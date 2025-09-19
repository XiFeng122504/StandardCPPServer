# 新手教学：从零搭建 epoll + 线程池聊天室服务器

> 建议边看边实践，把文中的命令在终端里亲自敲一遍。

## 0. 环境准备

- 操作系统：Linux（Ubuntu、Debian、Arch 等都可以）。
- 编译器：g++ 9 以上（需要 C++17）。
- 构建工具：CMake 3.16+。

验证命令：

```bash
cmake --version
g++ --version
```

## 1. 获取代码

```bash
git clone <你的仓库地址>
cd StandardCPPServer
```

目录结构说明：

```
├── CMakeLists.txt        # 构建脚本
├── include               # 头文件目录（对外接口 + 工具类）
│   ├── chat_server.h
│   └── thread_pool.h
├── src                   # 源文件目录
│   ├── chat_server.cpp
│   └── main.cpp
└── docs                  # 文档目录
    ├── annotated_walkthrough.md
    └── tutorial.md
```

## 2. 构建与运行

```bash
cmake -S . -B build
cmake --build build
./build/chat_server 5555
```

> 如果想换端口，只需要把最后一个参数改掉即可。

## 3. 打开两个终端测试

1. 终端 A：运行服务器 `./build/chat_server`。
2. 终端 B：使用 `nc` 连接服务器 `nc 127.0.0.1 5555`。
3. 终端 C：再打开一个 `nc 127.0.0.1 5555`。

现在在终端 B 输入任何内容，终端 C 都会收到同样的内容（反之亦然）。

## 4. 自己动手拓展功能

### 4.1 定制回调

在 `src/main.cpp` 里我们已经示范了如何注入回调。如果你希望在有人加入时发送欢迎消息，可以这么写：

```cpp
server.set_on_client_connected([&server](int fd, const sockaddr_in& addr) {
    (void)addr; // 暂时用不到 IP 信息
    const std::string welcome = "Welcome! 输入内容即可广播给所有人\n";
    ::send(fd, welcome.data(), welcome.size(), MSG_NOSIGNAL);
});
```

### 4.2 消息协议

当前实现是一个“纯文本广播”。如果想把消息改成“昵称: 内容”的格式，可以在 `on_message` 回调里解析：

```cpp
server.set_on_message([](int fd, const std::string& message) {
    std::cout << "[DEBUG] 原始数据: " << message << std::endl;
    // TODO: 在这里解析命令，比如 /nick Alice
});
```

### 4.3 优雅退出

1. 在 `ChatServer` 里添加 `stop()` 方法，把 `running_` 设为 `false`。
2. 在 `main` 里捕获 `SIGINT`，调用 `stop()` 并唤醒 `epoll_wait`。
3. 可以用 `eventfd` 或往监听 socket 写数据来唤醒阻塞的 `epoll_wait`。

## 5. 常见问题排查

| 问题 | 可能原因 | 解决思路 |
| ---- | -------- | -------- |
| 编译报错 `pthread` | 忘记安装 `libpthread`（glibc 自带）或链接 | 确认 `target_link_libraries` 已包含 `pthread` |
| 运行时报 `Address already in use` | 端口被占用 | 改端口，或等待旧进程释放端口 |
| 客户端连接后立刻断开 | 防火墙或 `send` 失败 | 查看服务器日志，确认没有触发 `close_client` |

## 6. 推荐的下一步学习路线

1. 了解 `epoll` 的边缘触发（ET）模式，并尝试改写 `ChatServer`。
2. 引入日志库（如 spdlog）替换 `std::cout`，了解日志级别与格式化输出。
3. 为线程池增加任务优先级或动态扩容能力。
4. 使用 `std::span`（C++20）或 `asio` 等库体验不同的网络抽象。

祝学习愉快！
