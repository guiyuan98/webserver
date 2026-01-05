# 项目技术栈清单

## 📋 完整技术栈

### 编程语言
- **C++14** - 现代 C++ 特性（智能指针、lambda、线程、RAII）

### 系统编程
- **Linux** - 操作系统
- **epoll** - I/O 多路复用（Level Triggered 模式）
- **Socket 编程** - TCP/IP 网络编程
- **非阻塞 I/O** - 提高并发性能
- **信号处理** - SIGINT/SIGTERM 优雅关闭

### 并发编程
- **多线程** - std::thread
- **互斥锁** - std::mutex
- **条件变量** - std::condition_variable
- **原子操作** - std::atomic
- **线程局部存储** - thread_local（内存池 TLS）

### 数据结构与算法
- **STL 容器**：
  - std::vector（动态数组）
  - std::queue（任务队列）
  - std::unordered_map（连接映射）
  - std::map（参数映射）
  - std::list（空闲链表）
- **STL 算法**：排序、查找等

### 内存管理
- **智能指针**：
  - std::unique_ptr（独占所有权）
  - std::shared_ptr（共享所有权）
- **RAII** - 资源获取即初始化
- **内存池** - 自定义内存管理
- **缓存行对齐** - 优化内存访问

### 数据库
- **MySQL** - 关系型数据库
- **MySQL C API** - libmysqlclient
- **SQL** - 数据查询和操作
- **连接池** - 数据库连接复用

### 网络协议
- **HTTP/1.1** - Web 协议
- **TCP/IP** - 传输层协议
- **Socket API** - 网络编程接口

### 加密与安全
- **OpenSSL** - 加密库
- **SHA256** - 密码哈希算法

### 设计模式
- **Reactor 模式** - 事件驱动架构
- **RAII 模式** - 资源管理
- **单例模式** - 日志系统
- **工厂模式** - 对象创建

### 开发工具
- **GCC/G++** - 编译器
- **Make** - 构建工具
- **Git** - 版本控制
- **GDB** - 调试工具（可选）

### 测试工具
- **wrk** - HTTP 压测工具
- **curl** - HTTP 客户端测试

### 标准库
- **STL** - 标准模板库
- **C++11/14 特性**：
  - auto 类型推导
  - lambda 表达式
  - 右值引用和移动语义
  - 智能指针
  - 线程库
  - 时间库（chrono）

---

## 🎯 简历技术栈描述（不同版本）

### 版本一：详细版（适合技术岗位）

**技术栈：**
- **语言**：C++14
- **系统编程**：Linux、epoll、Socket 编程、非阻塞 I/O
- **并发编程**：多线程、互斥锁、条件变量、原子操作
- **内存管理**：智能指针、RAII、自定义内存池
- **数据库**：MySQL、连接池
- **网络协议**：HTTP/1.1、TCP/IP
- **加密**：OpenSSL、SHA256
- **设计模式**：Reactor、RAII、单例
- **工具**：GCC、Make、Git、wrk

---

### 版本二：精简版（推荐，适合大多数岗位）

**技术栈：**
C++14、Linux、epoll、多线程、MySQL、HTTP/1.1、OpenSSL、STL、RAII、Reactor 模式

---

### 版本三：分类版（适合简历项目描述）

**核心技术：**
- 编程语言：C++14
- 系统编程：Linux、epoll、Socket 编程
- 并发模型：多线程、互斥锁、条件变量

**关键技术：**
- 架构模式：Reactor 事件驱动、非阻塞 I/O
- 资源管理：内存池、连接池、RAII
- 数据库：MySQL、连接池

**辅助技术：**
- 网络协议：HTTP/1.1、TCP/IP
- 加密：OpenSSL、SHA256
- 工具：Make、Git、wrk

---

### 版本四：关键词版（适合简历技能栏）

**编程语言**：C++14

**系统编程**：Linux、epoll、Socket、非阻塞 I/O

**并发编程**：多线程、互斥锁、条件变量、原子操作

**数据库**：MySQL、连接池

**网络协议**：HTTP/1.1、TCP/IP

**设计模式**：Reactor、RAII、单例

**工具**：GCC、Make、Git、wrk

---

## 📝 简历技能栏写法示例

### 示例一：分类写法

**编程语言**：C++14

**系统编程**：Linux、epoll、Socket 编程、非阻塞 I/O

**并发编程**：多线程、互斥锁、条件变量、原子操作、线程局部存储

**内存管理**：智能指针、RAII、自定义内存池、缓存行对齐

**数据库**：MySQL、连接池、SQL

**网络协议**：HTTP/1.1、TCP/IP

**设计模式**：Reactor 事件驱动、RAII、单例

**开发工具**：GCC、Make、Git、wrk

---

### 示例二：简洁写法

**技术栈**：C++14 | Linux | epoll | 多线程 | MySQL | HTTP/1.1 | OpenSSL | Reactor 模式 | RAII

---

### 示例三：详细写法

**编程语言**：C++14（智能指针、lambda、多线程、RAII）

**系统编程**：Linux、epoll（I/O 多路复用）、Socket 编程、非阻塞 I/O

**并发编程**：std::thread、std::mutex、std::condition_variable、std::atomic

**数据库**：MySQL、MySQL C API、连接池、SQL

**网络协议**：HTTP/1.1、TCP/IP

**加密**：OpenSSL、SHA256

**设计模式**：Reactor 事件驱动、RAII、单例、工厂模式

**开发工具**：GCC、Make、Git、wrk（压测）

---

## 🎯 根据岗位调整重点

### 后端开发岗位
**重点突出**：
- C++14、Linux、epoll、多线程
- MySQL、HTTP/1.1
- Reactor 模式、性能优化

### 系统开发岗位
**重点突出**：
- Linux、epoll、Socket 编程
- 内存管理、并发编程
- 系统优化、资源管理

### 网络编程岗位
**重点突出**：
- epoll、Socket 编程、非阻塞 I/O
- HTTP/1.1、TCP/IP
- Reactor 模式、事件驱动

### 数据库开发岗位
**重点突出**：
- MySQL、连接池
- SQL 优化
- 数据持久化

---

## 💡 技术栈描述技巧

### 1. 按熟练程度分类

**精通**：C++14、Linux、epoll、多线程

**熟练**：MySQL、HTTP/1.1、Reactor 模式

**了解**：OpenSSL、设计模式

### 2. 按项目使用分类

**核心技术**：C++14、epoll、多线程、MySQL

**关键技术**：Reactor 模式、内存池、连接池

**辅助技术**：OpenSSL、HTTP/1.1、STL

### 3. 使用关键词

- 使用行业通用术语
- 突出核心技术栈
- 避免过于冷门的技术
- 与岗位要求匹配

---

## 📊 技术栈使用统计

### 代码中使用的主要技术

| 技术 | 使用场景 | 重要程度 |
|------|----------|----------|
| **C++14** | 整个项目 | ⭐⭐⭐⭐⭐ |
| **epoll** | Reactor 事件循环 | ⭐⭐⭐⭐⭐ |
| **多线程** | 线程池、异步处理 | ⭐⭐⭐⭐⭐ |
| **MySQL** | 数据持久化 | ⭐⭐⭐⭐ |
| **HTTP/1.1** | 协议解析 | ⭐⭐⭐⭐ |
| **RAII** | 资源管理 | ⭐⭐⭐⭐ |
| **智能指针** | 内存管理 | ⭐⭐⭐ |
| **OpenSSL** | 密码加密 | ⭐⭐⭐ |
| **STL** | 数据结构 | ⭐⭐⭐ |

---

## 🎯 推荐简历写法

### 最推荐（简洁有力）

**技术栈**：
C++14 | Linux | epoll | 多线程 | MySQL | HTTP/1.1 | Reactor 模式 | RAII

**或分类写法**：

**编程语言**：C++14

**系统编程**：Linux、epoll、Socket 编程、非阻塞 I/O

**并发编程**：多线程、互斥锁、条件变量

**数据库**：MySQL、连接池

**网络协议**：HTTP/1.1、TCP/IP

**设计模式**：Reactor 事件驱动、RAII

---

## 📝 完整简历技能栏示例

```
【技术栈】

编程语言：
  • C++14（智能指针、lambda、多线程、RAII）

系统编程：
  • Linux、epoll（I/O 多路复用）、Socket 编程、非阻塞 I/O

并发编程：
  • 多线程（std::thread）、互斥锁（std::mutex）、条件变量（std::condition_variable）

数据库：
  • MySQL、MySQL C API、连接池、SQL

网络协议：
  • HTTP/1.1、TCP/IP

设计模式：
  • Reactor 事件驱动、RAII、单例模式

开发工具：
  • GCC、Make、Git、wrk（压测）
```

---

**建议**：根据目标岗位选择合适的技术栈描述方式，突出相关技术！

