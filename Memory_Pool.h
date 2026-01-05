#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// 内存块头部信息结构（用于追踪和管理）
struct Memory_Block_Header {
    size_t size;                                            // 实际分配的大小
    size_t block_size;                                      // 内存池块大小级别
    bool in_use;                                            // 是否正在使用
    void *pool_ptr;                                         // 指向所属的内存池（用于释放）
    std::chrono::steady_clock::time_point last_return_time; // 最后一次归还时间
};

// 单级内存池（管理特定大小的内存块）
class Fixed_Size_Pool {
  private:
    size_t block_size_;                       // 块大小
    size_t alignment_;                        // 对齐大小
    std::list<void *> free_blocks_;           // 空闲块链表
    std::mutex mutex_;                        // 保护该池的互斥锁
    std::atomic<size_t> current_used_;    // 当前使用中的块数
    std::atomic<size_t> current_free_;    // 当前空闲的块数
    std::atomic<size_t> total_allocated_; // 总分配块数
  public:
    Fixed_Size_Pool(size_t block_size, size_t alignment = 8)
        : block_size_(block_size), alignment_(alignment), mutex_(), 
          current_used_(0), current_free_(0), total_allocated_(0) {
        // 确保块大小是对齐大小的倍数
        block_size_ = (block_size_ + alignment_ - 1) & ~(alignment_ - 1);
    }

    // 分配一个内存块
    void *allocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!free_blocks_.empty()) {
            // 快速路径：从空闲链表获取
            void *ptr = free_blocks_.front();
            free_blocks_.pop_front();
            current_used_++;
            current_free_--;
            return ptr;
        }

        // 慢速路径：需要分配新块
        // 计算总大小：头部 + 对齐后的块大小
        size_t header_size = (sizeof(Memory_Block_Header) + alignment_ - 1) & ~(alignment_ - 1);
        size_t total_size = header_size + block_size_;

        // 分配内存
        void *raw_ptr = std::malloc(total_size);
        if (!raw_ptr) {
            return nullptr;
        }

        // 设置头部信息
        Memory_Block_Header *header = reinterpret_cast<Memory_Block_Header *>(raw_ptr);
        header->size = block_size_;
        header->block_size = block_size_;
        header->in_use = true;
        header->pool_ptr = this;

        // 返回用户可用的内存地址（跳过头部）
        void *user_ptr = static_cast<char *>(raw_ptr) + header_size;
        current_used_++;
        total_allocated_++;
        return user_ptr;
    }

    // 释放一个内存块
    void deallocate(void *ptr) {
        if (!ptr) {
            return;
        }

        // 从用户指针计算头部地址
        size_t header_size =
            (sizeof(Memory_Block_Header) + alignment_ - 1) & ~(alignment_ - 1);
        Memory_Block_Header *header =
            reinterpret_cast<Memory_Block_Header *>(static_cast<char *>(ptr) - header_size);

        // 验证头部信息
        if (header->pool_ptr != this || !header->in_use) {
            // 无效的释放操作，可能是双重释放
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        header->in_use = false;
        header->last_return_time = std::chrono::steady_clock::now();
        free_blocks_.push_back(ptr);
        current_used_--;
        current_free_++;
    }

    size_t get_block_size() const { return block_size_; }
    size_t get_current_used() const { return current_used_; }
    size_t get_current_free() const { return current_free_; }
    size_t get_total_allocated() const { return total_allocated_; }

    // 获取空闲块数量（用于统计）
    size_t get_free_block_count() const { return current_free_.load(); }

    // 清理过多的空闲块（释放部分空闲块回操作系统）
    // max_free_blocks: 保留的最大空闲块数，超过的部分会被释放
    size_t cleanup_excess_blocks(size_t max_free_blocks) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t released_count = 0;

        // 如果空闲块数量超过限制，释放多余的部分
        while (free_blocks_.size() > max_free_blocks && !free_blocks_.empty()) {
            void *ptr = free_blocks_.front();
            free_blocks_.pop_front();

            // 从用户指针计算原始内存地址（需要找到头部）
            size_t header_size =
                (sizeof(Memory_Block_Header) + alignment_ - 1) & ~(alignment_ - 1);
            void *raw_ptr = static_cast<char *>(ptr) - header_size;

            // 释放内存回操作系统
            std::free(raw_ptr);
            current_free_--;
            total_allocated_--;
            released_count++;
        }
        return released_count;
    }

    // 清理超时的空闲块（基于最后归还时间）
    // idle_timeout: 空闲超时时间，超过这个时间的空闲块会被释放
    size_t cleanup_idle_blocks_by_time(std::chrono::seconds idle_timeout) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t released_count = 0;
        auto now = std::chrono::steady_clock::now();

        // 遍历空闲块链表，找出超时的块
        std::list<void *> valid_blocks;
        size_t header_size =
            (sizeof(Memory_Block_Header) + alignment_ - 1) & ~(alignment_ - 1);

        while (!free_blocks_.empty()) {
            void *ptr = free_blocks_.front();
            free_blocks_.pop_front();

            // 从用户指针计算头部地址
            Memory_Block_Header *header =
                reinterpret_cast<Memory_Block_Header *>(static_cast<char *>(ptr) - header_size);

            // 检查是否超时
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - header->last_return_time);
            if (elapsed >= idle_timeout) {
                // 超时了，释放这个块
                void *raw_ptr = static_cast<char *>(ptr) - header_size;
                std::free(raw_ptr);
                current_free_--;
                total_allocated_--;
                released_count++;
            } else {
                // 未超时，保留这个块
                valid_blocks.push_back(ptr);
            }
        }

        // 将有效的块放回空闲链表
        free_blocks_ = std::move(valid_blocks);
        return released_count;
    }
};

// 线程局部缓存（每个线程维护自己的小块内存缓存）
struct Thread_Local_Cache {
    std::vector<void *> cache[8]; // 8个不同大小的缓存（对应8、16、32、64、128、256、512、1024字节）
    size_t cache_size = 16;       // 每个缓存的最大容量

    ~Thread_Local_Cache() {
        // 析构时清理缓存（需要归还到全局池）
        // 注意：这里需要访问全局池，但为了避免循环依赖，由外部处理
    }
};

class Memory_Pool {
  private:
    // 内存池配置结构
    struct PoolConfig {
        size_t small_block_sizes[8] = {8, 16, 32, 64, 128, 256, 512, 1024}; // 小块内存级别
        size_t max_total_memory = 1024 * 1024 * 1024;                       // 最大总内存（1GB）
        size_t alignment = 8;                                               // 内存对齐大小
        bool enable_tls = true;                                             // 是否启用线程局部存储
        size_t tls_cache_size = 16;                                         // 每个线程的缓存大小
        std::chrono::seconds cleanup_interval =
            std::chrono::seconds(30); // 清理间隔
        std::chrono::seconds idle_timeout =
            std::chrono::seconds(300); // 空闲超时时间
    } config_;

    // 内存池统计信息
    struct PoolStats {
        std::atomic<size_t> total_allocated; // 总分配内存大小（字节）
        std::atomic<size_t> total_freed;     // 总释放内存大小（字节）
        std::atomic<size_t> current_used;    // 当前使用中的内存大小（字节）
        std::atomic<size_t> current_free;    // 当前空闲的内存大小（字节）
        std::atomic<size_t> alloc_count;     // 分配次数
        std::atomic<size_t> free_count;      // 释放次数
        std::atomic<size_t> fragment_count;  // 碎片数量
    } stats_;

    std::vector<std::unique_ptr<Fixed_Size_Pool>> pools_; // 多级内存池数组
    std::mutex mutex_;                                    // 全局互斥锁（用于大块内存）
    std::atomic<bool> shutdown_;                          // 内存池是否关闭
    std::thread cleaner_thread_;                          // 清理线程

    // 根据大小找到对应的内存池索引（-1表示使用系统malloc）
    int find_pool_index(size_t size) {
        for (int i = 0; i < 8; ++i) {
            if (size <= config_.small_block_sizes[i]) {
                return i;
            }
        }
        return -1; // 大于1024字节，使用系统malloc
    }

    // 获取线程局部缓存（使用函数内静态thread_local避免ODR问题）
    Thread_Local_Cache *get_tls_cache() {
        if (!config_.enable_tls) {
            return nullptr;
        }
        static thread_local Thread_Local_Cache *tls_cache = nullptr; // 线程局部缓存
        static thread_local bool tls_initialized = false;            // 线程局部缓存是否初始化

        if (!tls_initialized) {
            tls_cache = new Thread_Local_Cache();
            tls_cache->cache_size = config_.tls_cache_size;
            tls_initialized = true;
        }
        return tls_cache;
    }

    // 从线程局部缓存分配
    void *allocate_from_tls(int pool_index) {
        Thread_Local_Cache *cache = get_tls_cache();
        if (!cache || pool_index < 0 || pool_index >= 8) {
            return nullptr;
        }

        if (!cache->cache[pool_index].empty()) {
            // 从TLS缓存获取
            void *ptr = cache->cache[pool_index].back();
            cache->cache[pool_index].pop_back();
            return ptr;
        }
        return nullptr;
    }

    // 归还到线程局部缓存
    bool return_to_tls(int pool_index, void *ptr) {
        Thread_Local_Cache *cache = get_tls_cache();
        if (!cache || pool_index < 0 || pool_index >= 8 || !ptr) {
            return false;
        }

        if (cache->cache[pool_index].size() < cache->cache_size) {
            // TLS缓存未满，直接加入
            cache->cache[pool_index].push_back(ptr);
            return true;
        }
        return false; // TLS缓存已满，需要归还到全局池
    }

    // 清理空闲内存块
    // 注意：固定大小池设计已经避免了碎片问题（相同大小的块可以完美复用）
    // 每30秒执行一次，清理：
    // 1. 基于空闲时间的清理：释放超过 idle_timeout 的空闲块
    // 2. 基于数量的清理：限制每个池的空闲块数量
    void cleanup_idle_blocks() {
        size_t total_released = 0;

        // 1. 基于时间的清理：清理超过空闲超时时间的块
        for (auto &pool : pools_) {
            size_t released = pool->cleanup_idle_blocks_by_time(config_.idle_timeout);
            total_released += released;
        }

        // 2. 基于数量的清理：每个池最多保留的空闲块数量
        const size_t MAX_FREE_BLOCKS_PER_POOL = 100;
        for (auto &pool : pools_) {
            size_t free_count = pool->get_free_block_count();
            if (free_count > MAX_FREE_BLOCKS_PER_POOL) {
                size_t released = pool->cleanup_excess_blocks(MAX_FREE_BLOCKS_PER_POOL);
                total_released += released;
            }
        }

        // 3. 检查总内存使用是否超过限制
        size_t current_total = stats_.total_allocated.load();
        if (current_total > config_.max_total_memory) {
            // 如果超过限制，更激进的清理：每个池只保留 10 个空闲块
            for (auto &pool : pools_) {
                size_t released = pool->cleanup_excess_blocks(10);
                total_released += released;
            }
        }

        // 更新当前空闲内存统计
        size_t current_free = 0;
        for (const auto &pool : pools_) {
            size_t free_count = pool->get_free_block_count();
            current_free += free_count * pool->get_block_size();
        }
        stats_.current_free.store(current_free);

        // 固定大小池没有碎片问题，碎片计数设为0
        stats_.fragment_count.store(0);
    }

    // 清理线程函数
    void cleanup_thread_func() {
        while (!shutdown_) {
            std::this_thread::sleep_for(config_.cleanup_interval);
            cleanup_idle_blocks();
        }
    }

    // 初始化内存池
    void initialize_pools() {
        for (size_t i = 0; i < 8; ++i) {
            pools_.push_back(
                std::make_unique<Fixed_Size_Pool>(config_.small_block_sizes[i], config_.alignment));
        }
    }

  public:
    // 构造函数
    Memory_Pool(size_t max_total_memory = 1024 * 1024 * 1024, bool enable_tls = true,
                size_t alignment = 8)
        : shutdown_(false) {
        config_.max_total_memory = max_total_memory;
        config_.enable_tls = enable_tls;
        config_.alignment = alignment;
        stats_.total_allocated = 0;
        stats_.total_freed = 0;
        stats_.current_used = 0;
        stats_.current_free = 0;
        stats_.alloc_count = 0;
        stats_.free_count = 0;
        stats_.fragment_count = 0;

        initialize_pools();
        cleaner_thread_ = std::thread(&Memory_Pool::cleanup_thread_func, this);
    }

    // 析构函数
    ~Memory_Pool() {
        shutdown_ = true;
        if (cleaner_thread_.joinable()) {
            cleaner_thread_.join();
        }
        // 清理所有内存池
        pools_.clear();
    }

    // 禁止拷贝和移动
    Memory_Pool(const Memory_Pool &) = delete;
    Memory_Pool &operator=(const Memory_Pool &) = delete;
    Memory_Pool(Memory_Pool &&) = delete;
    Memory_Pool &operator=(Memory_Pool &&) = delete;

    // 分配内存（主要接口）
    void *allocate(size_t size) {
        if (shutdown_ || size == 0) {
            return nullptr;
        }

        stats_.alloc_count++;

        // 找到对应的内存池索引
        int pool_index = find_pool_index(size);

        if (pool_index >= 0) {
            // 小块内存：使用内存池
            // 快速路径：优先从TLS缓存分配
            void *ptr = allocate_from_tls(pool_index);
            if (ptr) {
                stats_.total_allocated += config_.small_block_sizes[pool_index];
                stats_.current_used += config_.small_block_sizes[pool_index];
                return ptr;
            }

            // 慢速路径：从全局池分配
            ptr = pools_[pool_index]->allocate();
            if (ptr) {
                stats_.total_allocated += config_.small_block_sizes[pool_index];
                stats_.current_used += config_.small_block_sizes[pool_index];
                stats_.current_free -= config_.small_block_sizes[pool_index];
                return ptr;
            }
            // 分配失败，返回nullptr
            return nullptr;
        } else {
            // 大块内存：直接使用系统malloc
            void *ptr = std::malloc(size);
            if (ptr) {
                stats_.total_allocated += size;
                stats_.current_used += size;
            }
            return ptr;
        }
    }

    // 释放内存（主要接口）
    void deallocate(void *ptr) {
        if (!ptr) {
            return;
        }

        stats_.free_count++;

        // 从指针获取头部信息（需要计算偏移）
        size_t header_size =
            (sizeof(Memory_Block_Header) + config_.alignment - 1) & ~(config_.alignment - 1);
        Memory_Block_Header *header =
            reinterpret_cast<Memory_Block_Header *>(static_cast<char *>(ptr) - header_size);

        // 检查是否是内存池分配的内存
        if (header->pool_ptr != nullptr) {
            // 找到对应的内存池索引
            int pool_index = find_pool_index(header->block_size);
            if (pool_index >= 0 && header->pool_ptr == pools_[pool_index].get()) {
                // 小块内存：尝试归还到TLS缓存
                if (return_to_tls(pool_index, ptr)) {
                    // 成功归还到TLS，不更新全局统计
                    return;
                }

                // TLS缓存已满，归还到全局池
                pools_[pool_index]->deallocate(ptr);
                stats_.total_freed += config_.small_block_sizes[pool_index];
                stats_.current_used -= config_.small_block_sizes[pool_index];
                stats_.current_free += config_.small_block_sizes[pool_index];
                return;
            }
        }

        // 大块内存：使用系统free
        size_t size = header->size;
        std::free(header);
        stats_.total_freed += size;
        stats_.current_used -= size;
    }

    // 获取统计信息
    std::string get_stats() const {
        std::string result = "Memory Pool Stats:\n";
        result += "  Total Allocated: " + std::to_string(stats_.total_allocated.load()) + " bytes\n";
        result += "  Total Freed: " + std::to_string(stats_.total_freed.load()) + " bytes\n";
        result += "  Current Used: " + std::to_string(stats_.current_used.load()) + " bytes\n";
        result += "  Current Free: " + std::to_string(stats_.current_free.load()) + " bytes\n";
        result += "  Alloc Count: " + std::to_string(stats_.alloc_count.load()) + "\n";
        result += "  Free Count: " + std::to_string(stats_.free_count.load()) + "\n";
        result += "  Fragment Count: " + std::to_string(stats_.fragment_count.load()) + "\n";
        // 各池的统计信息
        for (size_t i = 0; i < pools_.size(); ++i) {
            result += "  Pool[" + std::to_string(i) + "] (Block Size: " +
                      std::to_string(config_.small_block_sizes[i]) + "): ";
            result += "Used=" + std::to_string(pools_[i]->get_current_used()) +
                      ", Free=" + std::to_string(pools_[i]->get_current_free()) +
                      ", Total=" + std::to_string(pools_[i]->get_total_allocated()) + "\n";
        }

        return result;
    }

    // 手动触发清理
    void cleanup() { cleanup_idle_blocks(); }

    // 获取配置
    PoolConfig get_config() const { return config_; }
};

// RAII 封装类：自动管理内存生命周期
class Memory_Pool_RAII {
  private:
    Memory_Pool &pool_; // 内存池引用
    void *memory_ptr_;  // 分配的内存指针
    size_t size_;       // 分配的内存大小

  public:
    // 构造函数：从内存池分配内存
    Memory_Pool_RAII(Memory_Pool &pool, size_t size)
        : pool_(pool), memory_ptr_(nullptr), size_(size) {
        memory_ptr_ = pool_.allocate(size);
        if (!memory_ptr_) {
            throw std::runtime_error("Memory allocation failed");
        }
    }
    // 析构函数：自动释放内存
    ~Memory_Pool_RAII() {
        if (memory_ptr_) {
            pool_.deallocate(memory_ptr_);
        }
    }
    // 禁止拷贝，允许移动
    Memory_Pool_RAII(const Memory_Pool_RAII &) = delete;
    Memory_Pool_RAII &operator=(const Memory_Pool_RAII &) = delete;
    Memory_Pool_RAII(Memory_Pool_RAII &&other) noexcept 
        : pool_(other.pool_), memory_ptr_(other.memory_ptr_), size_(other.size_) {
        other.memory_ptr_ = nullptr;
        other.size_ = 0;
    }
    Memory_Pool_RAII &operator=(Memory_Pool_RAII &&other) noexcept {
        if (this != &other) {
            if (memory_ptr_) {
                pool_.deallocate(memory_ptr_);
            }
            // pool_ 是引用，不能重新赋值，只能移动内存指针
            memory_ptr_ = other.memory_ptr_;
            size_ = other.size_;
            other.memory_ptr_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
    // 获取内存指针
    void *get() const { return memory_ptr_; }
    // 获取内存大小
    size_t size() const { return size_; }
    // 检查是否有效
    bool is_valid() const { return memory_ptr_ != nullptr; }
};
