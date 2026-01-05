#pragma once
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// 连接状态
enum class ConnectionState {
    CONNECTING, // 连接中
    READING,    // 读事件
    WRITING,    // 写事件
    CLOSING,    // 关闭中
    CLOSED      // 已关闭
};

// 连接信息结构
struct Connection {
    int fd;                                                 // 文件描述符
    ConnectionState state;                                  // 连接状态
    std::string read_buffer;                                // 读缓冲区
    std::string write_buffer;                               // 写缓冲区
    std::chrono::steady_clock::time_point last_active_time; // 最后活跃时间
    std::function<void(int, uint32_t)> event_handler;       // 事件处理回调

    Connection(int socket_fd)
        : fd(socket_fd),
          state(ConnectionState::CONNECTING),
          last_active_time(std::chrono::steady_clock::now()) {}

    ~Connection() {
        if (fd >= 0) {
            close(fd);
        }
    }

    // 禁止拷贝，允许移动
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    Connection(Connection &&) = default;
    Connection &operator=(Connection &&) = default;
};

// Reactor事件循环类
class Reactor {
  private:
    int epoll_fd_;                                                     // epoll文件描述符
    std::atomic<bool> running_;                                        // 是否运行
    std::unordered_map<int, std::shared_ptr<Connection>> connections_; // 连接映射
    std::mutex connections_mutex_;                                     // 连接互斥锁

    // 最大事件数
    static constexpr int MAX_EVENTS = 1024; // 最大事件数

    // 设置文件描述符为非阻塞
    bool set_nonblocking(int fd) { // 设置文件描述符为非阻塞
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            return false;
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            return false;
        }
        return true;
    }

    // 添加事件到epoll
    bool add_event(int fd, uint32_t events) { // 添加事件到epoll
        struct epoll_event ev;
        ev.events = events;
        ev.data.fd = fd;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            return false;
        }
        return true;
    }

    // 修改epoll事件
    bool modify_event(int fd, uint32_t events) { // 修改epoll事件
        struct epoll_event ev;
        ev.events = events;
        ev.data.fd = fd;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
            return false;
        }
        return true;
    }

    // 删除epoll事件
    bool delete_event(int fd) { // 删除epoll事件
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
            return false;
        }
        return true;
    }

  public:
    Reactor() : epoll_fd_(-1), running_(false) { // 初始化Reactor
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) {
            throw std::runtime_error("epoll_create1 failed: " + std::string(strerror(errno)));
        }
    }

    ~Reactor() { // 销毁Reactor
        stop();
        if (epoll_fd_ >= 0) {
            close(epoll_fd_);
        }
    }

    // 禁止拷贝和移动
    Reactor(const Reactor &) = delete;            // 禁止拷贝
    Reactor &operator=(const Reactor &) = delete; // 禁止赋值
    Reactor(Reactor &&) = delete;                 // 禁止移动
    Reactor &operator=(Reactor &&) = delete;      // 禁止赋值

    // 注册连接
    bool register_connection(int fd, std::function<void(int, uint32_t)> handler) { // 注册连接
        if (!set_nonblocking(fd)) {                                                // 设置文件描述符为非阻塞
            return false;
        }

        // 注册读事件（EPOLL LT模式，默认就是LT，不需要EPOLLET）
        if (!add_event(fd, EPOLLIN)) { // 添加读事件
            return false;
        }

        auto conn = std::make_shared<Connection>(fd); // 创建连接
        conn->event_handler = handler;                // 设置事件处理回调
        conn->state = ConnectionState::READING;       // 设置连接状态为读事件

        std::lock_guard<std::mutex> lock(connections_mutex_); // 加锁
        connections_[fd] = conn;                              // 添加连接
        return true;
    }

    // 更新连接事件
    bool update_connection(int fd, uint32_t events) { // 更新连接事件
        return modify_event(fd, events);
    }

    // 移除连接
    void remove_connection(int fd) { // 移除连接
        delete_event(fd);
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.erase(fd);
    }

    // 获取连接
    std::shared_ptr<Connection> get_connection(int fd) { // 获取连接
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(fd);
        if (it != connections_.end()) {
            return it->second;
        }
        return nullptr;
    }

    // 事件循环
    void event_loop(int timeout_ms = -1) {
        struct epoll_event events[MAX_EVENTS]; // 事件数组
        while (running_) {
            int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout_ms);
            if (num_events < 0) {
                if (errno == EINTR) {
                    continue; // 被信号中断，继续
                }
                break; // 其他错误，退出
            }
            // 处理事件
            for (int i = 0; i < num_events; ++i) {
                int fd = events[i].data.fd;
                uint32_t event_flags = events[i].events;
                // 获取连接
                auto conn = get_connection(fd);
                if (!conn) {
                    continue;
                }
                // 更新活跃时间
                conn->last_active_time = std::chrono::steady_clock::now();
                // 处理错误事件
                if (event_flags & (EPOLLERR | EPOLLHUP)) {
                    conn->state = ConnectionState::CLOSING;
                    if (conn->event_handler) {
                        conn->event_handler(fd, event_flags);
                    }
                    remove_connection(fd);
                    continue;
                }
                // 调用事件处理回调
                if (conn->event_handler) {
                    conn->event_handler(fd, event_flags);
                }
            }
        }
    }
    // 启动事件循环
    void start() {
        running_ = true;
    }

    // 停止事件循环
    void stop() {
        running_ = false;
    }

    // 检查是否运行中
    bool is_running() const {
        return running_.load();
    }

    // 获取连接数
    size_t get_connection_count() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(connections_mutex_));
        return connections_.size();
    }

    // 获取所有连接（返回连接对象列表）
    std::vector<std::shared_ptr<Connection>> get_all_connections() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(connections_mutex_));
        std::vector<std::shared_ptr<Connection>> result;
        result.reserve(connections_.size());
        for (const auto &pair : connections_) {
            result.push_back(pair.second);
        }
        return result;
    }

    // 获取所有连接的文件描述符
    std::vector<int> get_all_connection_fds() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(connections_mutex_));
        std::vector<int> result;
        result.reserve(connections_.size());
        for (const auto &pair : connections_) {
            result.push_back(pair.first);
        }
        return result;
    }
};
