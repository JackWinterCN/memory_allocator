#include "MemoryManager.h"
#include <thread>
#include <vector>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

// 打印内存统计信息
void printStats(const std::string& title, const MemoryStats& stats) {
    std::ostringstream oss;
    oss << "\n" << std::string(50, '-') << "\n";
    oss << title << ":\n";
    oss << "  Allocate Count: " << stats.allocate_count << "\n";
    oss << "  Deallocate Count: " << stats.deallocate_count << "\n";
    oss << "  Used Memory: " << stats.total_used_memory << " B\n";
    oss << "  Free Memory: " << stats.total_free_memory << " B\n";
    oss << "  Total Allocated: " << stats.total_allocated_memory << " B\n";
    oss << std::string(50, '-') << "\n";
    mutex_print(oss.str());
}

// 线程任务：模拟多线程内存分配/释放
void threadTask(int threadId) {
    std::cout << "\nThread " << threadId << " started";

    // 分配不同大小的内存（池化内存 + 超大内存）
    void* p1 = MemoryManager::allocate(64);    // 池化内存（64B用户可用）
    void* p2 = MemoryManager::allocate(1024);  // 池化内存（1024B用户可用）
    void* p3 = MemoryManager::allocate(4096);  // 超大内存（>2048B，直接malloc）
    void* p4 = MemoryManager::allocate(15);    // 动态块大小（15B→对齐后24B总大小）
    void* p5 = MemoryManager::allocate(0);     // 0字节→默认MIN_USER_SIZE（8B）

    std::ostringstream oss;
    oss  << "\nThread " << threadId << " allocated: "
              << p1 << "(" << 64 << "B), "
              << p2 << "(" << 1024 << "B), "
              << p3 << "(" << 4096 << "B), "
              << p4 << "(" << 15 << "B), "
              << p5 << "(" << 0 << "B→8B)";
    mutex_print(oss.str());
    // 释放内存（无需传入size！）
    MemoryManager::deallocate(p1);
    MemoryManager::deallocate(p2);
    MemoryManager::deallocate(p3);
    MemoryManager::deallocate(p4);
    MemoryManager::deallocate(p5);

    // 打印线程本地统计
    printStats("Thread " + std::to_string(threadId) + " Local Stats", MemoryManager::getLocalStats());
    std::cout << "Thread " << threadId << " finished";
}

int main() {
    std::cout << "Memory Manager Test Start\n";
    std::vector<std::thread> threads;

    // 创建10个线程模拟并发
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(threadTask, i);
    }

    // 等待所有线程退出（本地池内存转移到全局池）
    for (auto& t : threads) {
        t.join();
    }

    // 打印全局统计
    printStats("Global Memory Pool Final Stats", MemoryManager::getGlobalStats());

    // 主线程复用全局池内存
    void* p = MemoryManager::allocate(64);
    std::cout << "\nMain thread allocated (reuse global pool): " << p << "\n";
    MemoryManager::deallocate(p);

    // 再次打印全局统计（验证复用和释放）
    printStats("Global Memory Pool After Main Thread", MemoryManager::getGlobalStats());

    std::cout << "\nMemory Manager Test End\n";
    return 0;
}