#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

class Thread_Task {
  private:
    std::function<void()> task_func_;                        // 任务函数
    std::chrono::steady_clock::time_point last_submit_time;  // 任务提交时间
    std::chrono::steady_clock::time_point last_execute_time; // 任务执行时间
    std::atomic<bool> is_running_;                           // 任务是否正在执行
    std::atomic<bool> is_completed_;                         // 任务是否已完成

  public:
    Thread_Task(std::function<void()> func) : task_func_(func), is_running_(false), is_completed_(false) {
        last_submit_time = std::chrono::steady_clock::now();
    }

    // 执行任务
    void execute() {
        is_running_ = true;
        last_execute_time = std::chrono::steady_clock::now();
        if (task_func_) {
            task_func_();
        }
        is_running_ = false;
        is_completed_ = true;
    }

    // 检查任务是否有效
    bool is_valid() const { return task_func_ != nullptr; }

    // 检查任务是否正在运行
    bool is_running() const { return is_running_.load(); }

    // 检查任务是否已完成
    bool is_completed() const { return is_completed_.load(); }

    // 获取任务提交时间
    std::chrono::steady_clock::time_point get_last_submit_time() const { return last_submit_time; }

    // 获取任务执行时间
    std::chrono::steady_clock::time_point get_last_execute_time() const { return last_execute_time; }
};

// 工作线程封装类（类似 Mysql_Connection，但用于线程）
class Thread_Worker {
  private:
    std::thread worker_thread_;                           // 工作线程
    std::atomic<bool> is_idle_;                           // 是否空闲
    std::atomic<bool> should_stop_;                       // 是否应该停止
    std::chrono::steady_clock::time_point last_used_time; // 上次使用时间
    std::chrono::steady_clock::time_point last_idle_time; // 上次空闲时间

  public:
    Thread_Worker() : is_idle_(true), should_stop_(false) {
        last_used_time = std::chrono::steady_clock::now();
        last_idle_time = std::chrono::steady_clock::now();
    }

    ~Thread_Worker() {
        stop();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    // 启动工作线程
    void start(std::function<void()> work_loop) {
        should_stop_ = false;
        worker_thread_ = std::thread(work_loop);
    }

    // 停止工作线程
    void stop() {
        should_stop_ = true;
    }

    // 检查是否应该停止
    bool should_stop() const { return should_stop_.load(); }

    // 设置空闲状态
    void set_idle(bool idle) {
        is_idle_ = idle;
        if (idle) {
            last_idle_time = std::chrono::steady_clock::now();
        } else {
            last_used_time = std::chrono::steady_clock::now();
        }
    }

    // 检查是否空闲
    bool is_idle() const { return is_idle_.load(); }

    // 获取上次使用时间
    std::chrono::steady_clock::time_point get_last_used_time() const { return last_used_time; }

    // 获取上次空闲时间
    std::chrono::steady_clock::time_point get_last_idle_time() const { return last_idle_time; }

    // 分离线程
    void detach() {
        if (worker_thread_.joinable()) {
            worker_thread_.detach();
        }
    }

    // 等待线程完成
    void join() {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
};

class Thread_Pool // 线程池类
{
  private:
    struct PoolConfig // 线程池配置结构体
    {
        size_t min_threads;                // 最小线程数
        size_t max_threads;                // 最大线程数
        std::chrono::seconds task_timeout; // 任务获取超时时间
        std::chrono::seconds idle_timeout; // 线程空闲超时时间
        size_t queue_capacity;             // 任务队列容量（0表示无限制）
    } config_;

    struct PoolStats {
        std::atomic<int> total_threads;         // 当前线程总数
        std::atomic<int> idle_threads;          // 当前空闲线程数
        std::atomic<int> busy_threads;          // 当前忙碌线程数
        std::atomic<int> waiting_threads;       // 当前等待任务的线程数
        std::atomic<long long> total_tasks;     // 总任务数
        std::atomic<long long> completed_tasks; // 已完成任务数
        std::atomic<long long> pending_tasks;   // 等待中任务数
        std::atomic<long long> rejected_tasks;  // 被拒绝任务数
    } stats_;

    std::queue<std::shared_ptr<Thread_Task>> task_queue_; // 任务队列
    std::vector<std::unique_ptr<Thread_Worker>> workers_; // 工作线程数组
    std::mutex mutex_;                                    // 互斥锁
    std::condition_variable cv_;                          // 条件变量，用于通知等待的线程
    std::atomic<bool> shutdown_;                          // 线程池是否关闭
    std::thread cleaner_thread;                           // 清理线程

    // 创建新的工作线程
    std::unique_ptr<Thread_Worker> create_worker(bool add_to_idle = false) {
        auto worker = std::make_unique<Thread_Worker>();

        // 启动工作线程
        worker->start([this, worker_ptr = worker.get()]() {
            worker_loop(worker_ptr);
        });

        stats_.total_threads++;
        if (add_to_idle) {
            stats_.idle_threads++;
        } else {
            stats_.busy_threads++;
        }

        return worker;
    }

    // 工作线程循环函数
    void worker_loop(Thread_Worker *worker) {
        while (!shutdown_ && !worker->should_stop()) {
            std::shared_ptr<Thread_Task> task = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);

                // 标记为空闲
                worker->set_idle(true);
                stats_.waiting_threads++;
                stats_.idle_threads++;
                if (stats_.busy_threads > 0) {
                    stats_.busy_threads--;
                }

                // 等待任务或超时
                auto expire_time = std::chrono::steady_clock::now() + config_.task_timeout;
                bool got_task = cv_.wait_until(lock, expire_time, [this] {
                    return !task_queue_.empty() || shutdown_;
                });

                stats_.waiting_threads--;

                if (shutdown_) {
                    break;
                }

                if (got_task && !task_queue_.empty()) {
                    // 获取任务
                    task = task_queue_.front();
                    task_queue_.pop();
                    stats_.pending_tasks--;

                    // 标记为忙碌
                    worker->set_idle(false);
                    stats_.idle_threads--;
                    stats_.busy_threads++;
                } else {
                    // 超时，检查是否需要回收线程
                    if (stats_.total_threads > config_.min_threads) {
                        // 可以回收这个线程
                        stats_.idle_threads--;
                        if (stats_.total_threads > 0) {
                            stats_.total_threads--;
                        }
                        break; // 退出循环，线程结束
                    }
                    // 继续等待
                    continue;
                }
            }

            // 执行任务（不在锁内执行，避免长时间持有锁）
            if (task && task->is_valid()) {
                try {
                    task->execute();
                    stats_.completed_tasks++;
                } catch (...) {
                    // 任务执行异常，但不影响线程池
                }
            }
        }

        // 线程退出时的清理
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (worker->is_idle()) {
                stats_.idle_threads--;
            } else {
                stats_.busy_threads--;
            }
        }
    }

    // 清理空闲线程
    void cleanup_idle_threads() {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        std::vector<std::unique_ptr<Thread_Worker>> valid_workers;

        // 检查每个工作线程
        for (auto &worker : workers_) {
            if (!worker) {
                continue;
            }

            // 如果线程空闲时间超过超时时间，且线程数大于最小值，则回收
            if (worker->is_idle() && stats_.total_threads > config_.min_threads) {
                auto idle_duration = std::chrono::duration_cast<std::chrono::seconds>(
                    now - worker->get_last_idle_time());
                if (idle_duration >= config_.idle_timeout) {
                    // 停止这个线程
                    worker->stop();
                    stats_.total_threads--;
                    stats_.idle_threads--;
                    continue; // 不加入有效线程列表
                }
            }

            valid_workers.push_back(std::move(worker));
        }

        workers_ = std::move(valid_workers);

        // 确保线程数不少于最小值
        while (stats_.total_threads < config_.min_threads) {
            auto worker = create_worker(true);
            workers_.push_back(std::move(worker));
        }
    }

    // 清理线程函数
    void cleanup_thread_func() {
        while (!shutdown_) {
            std::this_thread::sleep_for(std::chrono::seconds(30)); // 每30秒清理一次
            cleanup_idle_threads();
        }
    }

  public:
    Thread_Pool(size_t min_threads = 5, size_t max_threads = 20,
                std::chrono::seconds task_timeout = std::chrono::seconds(5),
                std::chrono::seconds idle_timeout = std::chrono::seconds(300),
                size_t queue_capacity = 0)
        : shutdown_(false) {
        config_.min_threads = min_threads;
        config_.max_threads = max_threads;
        config_.task_timeout = task_timeout;
        config_.idle_timeout = idle_timeout;
        config_.queue_capacity = queue_capacity;

        stats_.total_threads = 0;
        stats_.idle_threads = 0;
        stats_.busy_threads = 0;
        stats_.waiting_threads = 0;
        stats_.total_tasks = 0;
        stats_.completed_tasks = 0;
        stats_.pending_tasks = 0;
        stats_.rejected_tasks = 0;

        // 初始化线程池
        for (size_t i = 0; i < config_.min_threads; ++i) {
            auto worker = create_worker(true);
            workers_.push_back(std::move(worker));
        }

        // 启动清理线程
        cleaner_thread = std::thread(&Thread_Pool::cleanup_thread_func, this);
    }

    ~Thread_Pool() {
        shutdown_ = true;
        cv_.notify_all(); // 通知所有等待的线程

        // 等待清理线程结束
        if (cleaner_thread.joinable()) {
            cleaner_thread.join();
        }

        // 停止所有工作线程
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &worker : workers_) {
            if (worker) {
                worker->stop();
            }
        }

        // 等待所有线程完成
        for (auto &worker : workers_) {
            if (worker) {
                worker->join();
            }
        }

        workers_.clear();
    }

    Thread_Pool(const Thread_Pool &) = delete; // 禁止拷贝
    Thread_Pool &operator=(const Thread_Pool &) = delete;

    // 提交任务（返回任务指针，用于 RAII）
    std::shared_ptr<Thread_Task> submit(std::function<void()> task_func) {
        if (shutdown_) {
            throw std::runtime_error("Thread pool is shut down");
        }

        if (!task_func) {
            throw std::invalid_argument("Task function is null");
        }

        auto task = std::make_shared<Thread_Task>(task_func);
        stats_.total_tasks++;

        std::unique_lock<std::mutex> lock(mutex_);

        // 检查队列容量
        if (config_.queue_capacity > 0 && task_queue_.size() >= config_.queue_capacity) {
            stats_.rejected_tasks++;
            stats_.total_tasks--;
            throw std::runtime_error("Task queue is full");
        }

        // 如果线程数未达到最大值，且没有空闲线程，创建新线程
        if (stats_.total_threads < config_.max_threads && stats_.idle_threads == 0 &&
            task_queue_.empty()) {
            lock.unlock();
            auto worker = create_worker(false);
            lock.lock();
            workers_.push_back(std::move(worker));
        }

        // 添加任务到队列
        task_queue_.push(task);
        stats_.pending_tasks++;

        // 通知等待的线程
        cv_.notify_one();

        return task;
    }

    // 执行任务（同步，等待完成）
    void execute(std::function<void()> task_func) {
        auto task = submit(task_func);
        // 等待任务完成
        while (!task->is_completed() && !shutdown_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // 等待所有任务完成
    void wait_for_idle() {
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (task_queue_.empty() && stats_.busy_threads == 0) {
                break;
            }
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // 获取线程池状态
    std::string get_stats() const {
        return "Thread Pool Stats:\n" + std::to_string(stats_.total_threads.load()) + "\n" +
               "  Idle Threads: " + std::to_string(stats_.idle_threads.load()) + "\n" +
               "  Busy Threads: " + std::to_string(stats_.busy_threads.load()) + "\n" +
               "  Waiting Threads: " + std::to_string(stats_.waiting_threads.load()) + "\n" +
               "  Total Tasks: " + std::to_string(stats_.total_tasks.load()) + "\n" +
               "  Completed Tasks: " + std::to_string(stats_.completed_tasks.load()) + "\n" + "  Pending Tasks: " + std::to_string(stats_.pending_tasks.load()) + "\n" + "  Rejected Tasks: " + std::to_string(stats_.rejected_tasks.load());
    }

    // 获取配置
    PoolConfig get_config() const { return config_; }
};

// 线程池任务 RAII 封装类
class Thread_Pool_Task_RAII {
  private:
    Thread_Pool &pool_;                 // 线程池引用
    std::shared_ptr<Thread_Task> task_; // 任务指针

  public:
    // 构造函数：提交任务到线程池
    Thread_Pool_Task_RAII(Thread_Pool &pool, std::function<void()> task_func)
        : pool_(pool) {
        task_ = pool_.submit(task_func);
    }

    // 析构函数：自动等待任务完成（如果需要）
    ~Thread_Pool_Task_RAII() {
        // 可以在这里等待任务完成，或只是持有任务引用
        // 任务会在线程池中自动执行完成
    }

    // 禁止拷贝和移动
    Thread_Pool_Task_RAII(const Thread_Pool_Task_RAII &) = delete;
    Thread_Pool_Task_RAII &operator=(const Thread_Pool_Task_RAII &) = delete;
    Thread_Pool_Task_RAII(Thread_Pool_Task_RAII &&) = delete;
    Thread_Pool_Task_RAII &operator=(Thread_Pool_Task_RAII &&) = delete;

    // 获取任务指针
    std::shared_ptr<Thread_Task> get_task() const { return task_; }

    // 检查任务是否有效
    bool is_valid() const { return task_ && task_->is_valid(); }

    // 检查任务是否正在运行
    bool is_running() const { return task_ && task_->is_running(); }

    // 检查任务是否已完成
    bool is_completed() const { return task_ && task_->is_completed(); }

    // 等待任务完成
    void wait() {
        if (task_) {
            while (!task_->is_completed()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
};
