# 代码逐行讲解（Annotated Walkthrough）

> 目标读者：第一次接触 Linux 下 C++ 网络编程的同学。
>
> 作用：帮助你理解每一块代码存在的意义，未来扩展时也能快速定位需要修改的地方。

## 架构全景

本项目由三个核心部分构成：

1. `ThreadPool` —— 一个可复用的线程池，负责异步广播消息。
2. `ChatServer` —— 业务核心，封装了 socket、epoll 以及连接管理逻辑。
3. `main` —— 入口函数，只做最小化的初始化与回调配置。

CMake 构建脚本会把 `src/main.cpp` 和 `src/chat_server.cpp` 编译成一个名为 `chat_server` 的可执行文件。

下文将按照“线程池 → 服务器类 → 入口”的顺序逐段讲解。

---

## include/thread_pool.h

### 设计理念

- **固定线程数**：构造函数里创建线程，析构时回收，避免频繁创建销毁线程的开销。
- **任务队列**：任何需要异步执行的函数都被包装后丢到 `std::queue` 里，工作线程会不断取出并执行。
- **阻塞等待**：通过 `std::condition_variable` 在任务队列为空时阻塞线程，避免空转。

### 关键成员说明

| 成员 | 作用 |
| ---- | ---- |
| `workers_` | 真正干活的线程列表 |
| `tasks_` | 待执行任务队列 |
| `mutex_` | 保护 `tasks_` 和 `stop_` |
| `condition_` | 通知工作线程有新任务 |
| `stop_` | 线程池是否已经停止 |

### enqueue 的流程

1. `std::packaged_task` 将任意可调用对象包裹成共享状态，便于获取返回值。
2. 通过锁把任务丢进队列。
3. `notify_one` 唤醒一个等待的工作线程。
4. 工作线程在 `workerLoop()` 中醒来，取出任务并执行。

> 小练习：尝试在 `workerLoop()` 中加入日志，打印当前线程 ID，观察任务在不同线程之间的调度情况。

---

## include/chat_server.h & src/chat_server.cpp

### 构造函数与析构函数

- 构造函数仅负责保存配置与构建线程池。
- 析构函数里保证所有资源（监听 socket、epoll fd、客户端 fd）都被关闭。即便 `run()` 抛出异常或提前返回，也不会泄露资源。

### run()

1. 调用 `prepare_listening_socket()` 创建监听 socket 并设置为非阻塞。
2. 调用 `register_listen_socket()` 将监听 fd 加入 epoll。
3. 开始 `event_loop()`，进入无限循环等待事件。

若任一步失败，会打印错误并返回 `false`，提示主函数退出。

### event_loop()

- `epoll_wait` 会阻塞直到有事件发生；返回值 `ready` 表示就绪事件数量。
- 对于监听 fd，调用 `handle_new_connection()`；
- 对于客户端 fd，调用 `handle_client_event()`。

### handle_new_connection()

- 使用 `accept` 接受所有准备好的连接，直到返回 `EAGAIN/EWOULDBLOCK`，表示没有更多等待的连接。
- 新连接设置成非阻塞，并加入 epoll 监听读事件。
- 将 fd 存入 `clients_` 集合，方便广播时获取在线列表。
- 如果设置了 `on_client_connected_` 回调，就调用；否则打印默认日志。

### handle_client_event()

- 任何错误事件（`EPOLLERR/EPOLLHUP/EPOLLRDHUP`）都会调用 `close_client()` 关闭连接。
- 对于可读事件：持续调用 `recv`，把数据拼接到 `message` 字符串里。
- 如果 `recv` 返回 0，说明对端主动断开；调用 `close_client()`。
- 如果成功读取到数据，触发 `on_message_` 回调，并把广播任务提交给线程池。

### broadcast_message()

- 先复制一份当前在线 fd 列表（快照），避免发送时长时间持有锁。
- 逐个发送消息，遇到 `EAGAIN/EWOULDBLOCK` 会继续尝试；其他错误或返回 0 则关闭对应客户端。

### close_client()

- 从 epoll 中移除该 fd。
- 关闭 fd，并从客户端集合里擦除。
- 如果设置了 `on_client_disconnected_`，就调用回调；否则打印日志。

> 扩展练习：尝试在回调里维护“用户昵称 → fd”的映射，这样就可以在 `on_message_` 中识别是谁发的消息。

---

## src/main.cpp

主函数保持极简：

1. 忽略 `SIGPIPE`，防止写入已关闭连接时进程异常退出。
2. 读取命令行参数作为监听端口。
3. 构造 `ChatServer` 并设置三个回调：连接、断开、收到消息。
4. 调用 `run()`，若失败则返回非零值给操作系统。

> 小练习：试着在命令行传入 `./chat_server 6666`，体验自定义端口。

---

## 下一步可以做什么？

1. **添加昵称/登录功能**：可以在 `on_client_connected_` 里发送欢迎消息，或在 `on_message_` 里解析命令。
2. **持久化聊天记录**：把 `on_message_` 里的内容写入文件或数据库。
3. **优雅退出**：增加一个信号处理器（如捕获 `SIGINT`），在其中把 `running_` 设为 `false`，让 `event_loop()` 退出。

希望这份文档能成为你继续深入学习的起点！
