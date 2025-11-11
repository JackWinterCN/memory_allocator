#include "MemoryManager.h"
#include <iostream>
#include <iomanip>

// -------------------------- 工具函数 --------------------------
void mutex_print(const std::string& msg) {
    static std::mutex log_mutex;
    std::lock_guard<std::mutex> log_lock(log_mutex);
    std::cout << msg << std::endl;
}


// 按对齐步长向上取整（步长必须是2的幂）
static size_t alignUp(size_t value, size_t alignment) {
    assert((alignment & (alignment - 1)) == 0 && "Alignment must be power of 2");
    return (value + alignment - 1) & ~(alignment - 1);
}

// 【关键修复】定义线程本地静态成员（每个线程独立实例，同一线程内共享）
thread_local ThreadLocalMemoryPool MemoryManager::local_pool_;

// -------------------------- BaseMemoryPool 实现 --------------------------
BaseMemoryPool::BaseMemoryPool() {
    // 初始化预设块大小（用户可用大小8~2048，对应块总大小=用户大小+头部）
    for (size_t user_size = MIN_USER_SIZE; user_size <= MAX_USER_SIZE; user_size += BLOCK_ALIGNMENT) {
        size_t total_size = calcAlignedTotalSize(user_size);
        addBlockSizeIfNotExist(total_size);
    }
}

size_t BaseMemoryPool::calcAlignedTotalSize(size_t user_size) const {
    if (user_size == 0) user_size = MIN_USER_SIZE;
    size_t total_size = user_size + FREE_BLOCK_HEADER_SIZE;
    return alignUp(total_size, BLOCK_ALIGNMENT);
}

int BaseMemoryPool::findBlockSizeIndex(size_t total_size) const {
    auto it = std::lower_bound(block_sizes_.begin(), block_sizes_.end(), total_size);
    if (it != block_sizes_.end() && *it == total_size) {
        return static_cast<int>(std::distance(block_sizes_.begin(), it));
    }
    return -1;
}

int BaseMemoryPool::addBlockSizeIfNotExist(size_t total_size) {
    // 检查是否已存在
    int index = findBlockSizeIndex(total_size);
    if (index != -1) return index;

    // 插入到有序列表
    auto it = std::lower_bound(block_sizes_.begin(), block_sizes_.end(), total_size);
    index = static_cast<int>(std::distance(block_sizes_.begin(), it));
    block_sizes_.insert(it, total_size);
    free_lists_.insert(free_lists_.begin() + index, nullptr);
    free_block_counts_.insert(free_block_counts_.begin() + index, 0);
    return index;
}

bool BaseMemoryPool::allocateBatch(size_t total_size, int index) {
    if (index < 0 || index >= static_cast<int>(block_sizes_.size())) return false;
    if (total_size == 0 || PAGE_SIZE < total_size) return false;

    // 计算一页能拆分的块数
    size_t block_count = PAGE_SIZE / total_size;
    if (block_count == 0) return false;

    // 批量分配一页内存
    void* page = malloc(PAGE_SIZE);
    if (!page) return false;

    // 拆分页为多个块，串联成空闲链表
    FreeBlock* prev = nullptr;
    for (size_t i = 0; i < block_count; ++i) {
        char* block_addr = static_cast<char*>(page) + i * total_size;
        FreeBlock* block = reinterpret_cast<FreeBlock*>(block_addr);
        block->size = total_size; // 设置块总大小
        block->next = nullptr;

        if (prev) prev->next = block;
        else free_lists_[index] = block; // 第一个块作为链表头
        prev = block;
    }

    // 更新统计信息
    free_block_counts_[index] += block_count;
    total_free_memory_ += total_size * block_count;
    total_allocated_memory_ += PAGE_SIZE;

    return true;
}

size_t BaseMemoryPool::reclaimIdleMemory() {
    size_t reclaimed_size = 0;

    // 遍历所有块大小链表，回收超出保留数量的空闲块
    for (int i = 0; i < static_cast<int>(block_sizes_.size()); ++i) {
        size_t total_size = block_sizes_[i];
        size_t current_count = free_block_counts_[i];
        if (current_count <= RESERVE_BLOCK_COUNT) continue;

        // 计算可释放的块数（按页对齐，保证释放整页内存）
        size_t release_count = current_count - RESERVE_BLOCK_COUNT;
        size_t blocks_per_page = PAGE_SIZE / total_size;
        if (blocks_per_page == 0) continue;

        release_count = (release_count / blocks_per_page) * blocks_per_page;
        if (release_count == 0) continue;

        // 从链表尾部找到要释放的页（需遍历链表，避免破坏链表结构）
        FreeBlock* head = free_lists_[i];
        FreeBlock* prev = nullptr;
        FreeBlock* release_head = head;
        size_t remaining = current_count - release_count;

        // 移动到要释放的第一个块
        for (size_t j = 0; j < remaining - 1; ++j) {
            if (!release_head->next) break;
            prev = release_head;
            release_head = release_head->next;
        }

        if (!release_head) continue;

        // 断开链表
        if (prev) prev->next = nullptr;
        else free_lists_[i] = nullptr;

        // 释放整页内存（所有释放的块必须连续在同一页）
        void* page = release_head;
        reclaimed_size += total_size * release_count;

        // 更新统计信息
        free_block_counts_[i] -= release_count;
        total_free_memory_ -= total_size * release_count;

        // 释放内存到系统
        free(page);
    }

    return reclaimed_size;
}

void* BaseMemoryPool::allocate(size_t user_size) {
    // 超大内存直接返回nullptr（交给malloc）
    if (user_size > MAX_USER_SIZE) return nullptr;

    // 计算块总大小
    size_t total_size = calcAlignedTotalSize(user_size);
    int index = findBlockSizeIndex(total_size);

    // 动态添加不存在的块大小
    if (index == -1) {
        index = addBlockSizeIfNotExist(total_size);
        if (index == -1) return nullptr;
    }

    // 空闲链表为空时，批量分配
    if (!free_lists_[index] && !allocateBatch(total_size, index)) {
        return nullptr;
    }

    // 从链表头取出一块
    FreeBlock* block = free_lists_[index];
    free_lists_[index] = block->next;

    // 更新统计信息
    free_block_counts_[index]--;
    total_free_memory_ -= block->size;
    allocate_count_++;

    // 返回用户数据区指针（跳过空闲块头部）
    return static_cast<char*>(reinterpret_cast<void*>(block)) + FREE_BLOCK_HEADER_SIZE;
}

void BaseMemoryPool::deallocate(void* user_ptr) {
    if (!user_ptr) return;

    // 计算块头部指针
    FreeBlock* block = reinterpret_cast<FreeBlock*>(
        static_cast<char*>(user_ptr) - FREE_BLOCK_HEADER_SIZE
    );
    size_t total_size = block->size;

    // 跳过超大块（直接free）和无效块
    if (total_size == 0 || total_size < calcAlignedTotalSize(MIN_USER_SIZE)) {
        free(user_ptr);
        return;
    }

    // 查找块大小索引
    int index = findBlockSizeIndex(total_size);
    if (index == -1) {
        free(user_ptr);
        return;
    }

    // 将块插入链表头（高效）
    block->next = free_lists_[index];
    free_lists_[index] = block;

    // 更新统计信息
    free_block_counts_[index]++;
    total_free_memory_ += total_size;
    deallocate_count_++;
}

void BaseMemoryPool::transferTo(BaseMemoryPool& dest) {
    // 遍历所有空闲链表，转移到目标池
    for (size_t i = 0; i < free_lists_.size(); ++i) {
        if (!free_lists_[i]) continue;

        size_t total_size = block_sizes_[i];
        int dest_index = dest.findBlockSizeIndex(total_size);

        // 目标池不存在该块大小，动态添加
        if (dest_index == -1) {
            dest_index = dest.addBlockSizeIfNotExist(total_size);
            if (dest_index == -1) continue;
        }

        // 连接当前链表到目标链表尾部
        FreeBlock* last = free_lists_[i];
        while (last->next) last = last->next;
        last->next = dest.free_lists_[dest_index];
        dest.free_lists_[dest_index] = free_lists_[i];

        // 更新目标池统计信息
        size_t block_count = free_block_counts_[i];
        dest.free_block_counts_[dest_index] += block_count;
        dest.total_free_memory_ += total_size * block_count;

        // 清空当前池链表
        free_lists_[i] = nullptr;
        free_block_counts_[i] = 0;
        total_free_memory_ -= total_size * block_count;
    }
}

MemoryStats BaseMemoryPool::getStats() const {
    MemoryStats stats;
    stats.allocate_count = allocate_count_;
    stats.deallocate_count = deallocate_count_;
    stats.total_free_memory = total_free_memory_;
    stats.total_allocated_memory = total_allocated_memory_;
    stats.total_used_memory = total_allocated_memory_ - total_free_memory_;
    return stats;
}

// -------------------------- GlobalMemoryPool 实现 --------------------------
GlobalMemoryPool& GlobalMemoryPool::getInstance() {
    static GlobalMemoryPool instance; // C++11线程安全单例
    return instance;
}

void* GlobalMemoryPool::allocate(size_t user_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_.allocate(user_size);
}

void GlobalMemoryPool::deallocate(void* user_ptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.deallocate(user_ptr);

    // 检查是否需要回收内存
    if (pool_.getStats().total_free_memory > MAX_GLOBAL_FREE_MEMORY) {
        size_t reclaimed = pool_.reclaimIdleMemory();
        if (reclaimed > 0) {
            // 可选：打印回收日志（生产环境可关闭）
            // std::cout << "[GlobalPool] Reclaimed " << reclaimed << " bytes\n";
        }
    }
}

void GlobalMemoryPool::transferFrom(BaseMemoryPool& src) {
    std::lock_guard<std::mutex> lock(mutex_);
    src.transferTo(pool_);

    // 转移后检查是否需要回收内存
    if (pool_.getStats().total_free_memory > MAX_GLOBAL_FREE_MEMORY) {
        pool_.reclaimIdleMemory();
    }
}

MemoryStats GlobalMemoryPool::getGlobalStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_.getStats();
}

// -------------------------- ThreadLocalMemoryPool 实现 --------------------------
ThreadLocalMemoryPool::~ThreadLocalMemoryPool() {
    // 线程退出时，将本地池内存转移到全局池
    GlobalMemoryPool::getInstance().transferFrom(pool_);
}

void* ThreadLocalMemoryPool::allocate(size_t user_size) {
    return pool_.allocate(user_size);
}

void ThreadLocalMemoryPool::deallocate(void* user_ptr) {
    return pool_.deallocate(user_ptr);
}

MemoryStats ThreadLocalMemoryPool::getLocalStats() const {
    return pool_.getStats();
}

// -------------------------- MemoryManager 实现 --------------------------
void* MemoryManager::allocate(size_t user_size) {
    // 1. 优先从【共享的线程本地池】分配（无锁）
    void* p = local_pool_.allocate(user_size);
    if (p) return p;

    // 2. 本地池不足，从全局池分配（加锁）
    p = GlobalMemoryPool::getInstance().allocate(user_size);
    if (p) return p;

    // 3. 全局池不足，直接malloc（对齐处理）
    size_t aligned_size = alignUp(user_size, BLOCK_ALIGNMENT);
    return malloc(aligned_size);
}

void MemoryManager::deallocate(void* user_ptr) {
    if (!user_ptr) return;

    // 尝试释放到【共享的线程本地池】（无锁）
    local_pool_.deallocate(user_ptr);
}

MemoryStats MemoryManager::getGlobalStats() {
    return GlobalMemoryPool::getInstance().getGlobalStats();
}

MemoryStats MemoryManager::getLocalStats() {
    // 访问共享的线程本地池统计
    return local_pool_.getLocalStats();
}