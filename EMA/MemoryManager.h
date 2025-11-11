#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cassert>

// 核心配置（可根据需求调整）
const size_t MIN_USER_SIZE = 8;         // 用户可请求的最小大小（字节）
const size_t MAX_USER_SIZE = 2048;      // 用户可请求的最大池化大小（超过直接malloc）
const size_t BLOCK_ALIGNMENT = 8;       // 内存对齐步长（必须是2的幂）
const size_t PAGE_SIZE = 4096;          // 批量分配的页大小（系统页大小通常为4096）
const size_t MAX_GLOBAL_FREE_MEMORY = 10 * 1024 * 1024; // 全局池最大空闲内存（10MB）
const size_t RESERVE_BLOCK_COUNT = 4;   // 内存回收时保留的最小块数（每类块）

// 内存统计结构体（支持全局/线程本地统计）
struct MemoryStats {
    size_t allocate_count = 0;          // 累计分配次数
    size_t deallocate_count = 0;        // 累计释放次数
    size_t total_free_memory = 0;       // 当前空闲内存（字节）
    size_t total_used_memory = 0;       // 当前使用内存（字节）
    size_t total_allocated_memory = 0;  // 累计分配总内存（字节）
};

// 空闲块链表节点（嵌入块头部，仅空闲时有效）
struct FreeBlock {
    size_t size;       // 块总大小（含当前头部）
    FreeBlock* next;   // 下一个空闲块指针
};
const size_t FREE_BLOCK_HEADER_SIZE = sizeof(FreeBlock); // 空闲块头部大小

// 基础内存池（线程本地池和全局池的基类）
class BaseMemoryPool {
public:
    BaseMemoryPool();
    ~BaseMemoryPool() = default;

    // 禁止拷贝构造和赋值（修复atomic拷贝错误）
    BaseMemoryPool(const BaseMemoryPool&) = delete;
    BaseMemoryPool& operator=(const BaseMemoryPool&) = delete;

    // 分配内存（user_size：用户实际需要的大小）
    void* allocate(size_t user_size);

    // 释放内存（无需传入size，自动从块头部读取）
    void deallocate(void* user_ptr);

    // 将当前池的所有空闲块转移到目标池
    void transferTo(BaseMemoryPool& dest);

    // 获取内存统计信息
    MemoryStats getStats() const;

    // 改为public：允许外部管理类触发内存回收（修复访问权限错误）
    size_t reclaimIdleMemory();

private:
    // 计算对齐后的块总大小（user_size + 头部大小，按BLOCK_ALIGNMENT对齐）
    size_t calcAlignedTotalSize(size_t user_size) const;

    // 查找块总大小对应的链表索引（不存在则返回-1）
    int findBlockSizeIndex(size_t total_size) const;

    // 动态添加块大小到列表（线程安全需外部保证）
    int addBlockSizeIfNotExist(size_t total_size);

    // 批量分配指定大小的块（填充到空闲链表）
    bool allocateBatch(size_t total_size, int index);

private:
    std::vector<size_t> block_sizes_;    // 支持的块总大小列表（有序）
    std::vector<FreeBlock*> free_lists_; // 空闲块链表（索引对应块大小）
    std::vector<size_t> free_block_counts_; // 每个链表的空闲块数

    // 统计信息（原子类型保证线程安全，本地池无锁但统计仍需原子性）
    std::atomic_size_t allocate_count_{0};
    std::atomic_size_t deallocate_count_{0};
    std::atomic_size_t total_free_memory_{0};
    std::atomic_size_t total_allocated_memory_{0};
};

// 全局内存池（单例模式，线程安全）
class GlobalMemoryPool {
public:
    static GlobalMemoryPool& getInstance();

    // 分配内存（加锁）
    void* allocate(size_t user_size);

    // 释放内存（加锁）
    void deallocate(void* user_ptr);

    // 接收其他池的内存转移（加锁，转移后触发内存回收）
    void transferFrom(BaseMemoryPool& src);

    // 获取全局内存统计（加锁）
    MemoryStats getGlobalStats();

    // 禁止拷贝构造和赋值
    GlobalMemoryPool(const GlobalMemoryPool&) = delete;
    GlobalMemoryPool& operator=(const GlobalMemoryPool&) = delete;

private:
    GlobalMemoryPool() = default;
    ~GlobalMemoryPool() = default;

private:
    BaseMemoryPool pool_;
    std::mutex mutex_; // 全局池访问锁
};

// 线程本地内存池（每个线程独立实例）
class ThreadLocalMemoryPool {
public:
    ThreadLocalMemoryPool() = default;
    ~ThreadLocalMemoryPool();

    // 禁止拷贝构造和赋值（避免隐含拷贝BaseMemoryPool）
    ThreadLocalMemoryPool(const ThreadLocalMemoryPool&) = delete;
    ThreadLocalMemoryPool& operator=(const ThreadLocalMemoryPool&) = delete;

    // 分配内存（无锁）
    void* allocate(size_t user_size);

    // 释放内存（无锁）
    void deallocate(void* user_ptr);

    // 获取线程本地内存统计（无锁）
    MemoryStats getLocalStats() const;

private:
    BaseMemoryPool pool_;
};

// 对外接口类（用户直接调用）
class MemoryManager {
private:
    // 【关键修复】线程本地池：同一个线程共享一个实例
    static thread_local ThreadLocalMemoryPool local_pool_;

public:
    // 分配内存（遵循：本地池→全局池→malloc）
    static void* allocate(size_t user_size);

    // 释放内存（池化内存放回本地池，超大内存直接free）
    static void deallocate(void* user_ptr);

    // 获取全局内存统计
    static MemoryStats getGlobalStats();

    // 获取当前线程的本地内存统计
    static MemoryStats getLocalStats();
};

void mutex_print(const std::string& msg);

#endif // MEMORY_MANAGER_H