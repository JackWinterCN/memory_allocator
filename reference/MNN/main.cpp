#include "core/BufferAllocator.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace MNN;

std::mutex mem_mutex;

// 线程任务：在group内进行内存分配和释放
void threadTask(EagerBufferAllocator *allocator, int threadId,
                std::vector<MemChunk> &threadChunks) {
  // 1. 开始线程组（线程隔离）
  allocator->beginGroup();
  std::cout << "Thread " << threadId << " start group" << std::endl;

  // 2. 分配多个内存块并记录
  for (int i = 0; i < 5; ++i) {
    {
      std::lock_guard<std::mutex> lock(mem_mutex);
      size_t allocSize =
          1024 * (i + 1); // 分配不同大小的内存（1KB, 2KB, ..., 5KB）
      MemChunk chunk = allocator->alloc(allocSize);
      assert(!chunk.invalid() && "Allocation failed");
      threadChunks.push_back(chunk);
      std::cout << "Thread " << threadId << " alloc: " << chunk.ptr()
                << " (size: " << allocSize << ")" << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // 3. 释放部分内存块（模拟中间释放操作）
  for (int i = 0; i < 3; ++i) {
    if (!threadChunks[i].invalid()) {
      std::lock_guard<std::mutex> lock(mem_mutex);
      std::cout << "Thread " << threadId
                << " free: " << static_cast<void *>(threadChunks[i].ptr())
                << std::endl;
      allocator->free(threadChunks[i]);
      threadChunks[i] = MemChunk(); // 标记为无效
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // 4. 结束线程组（释放隔离状态）
  allocator->endGroup();
  std::cout << "Thread " << threadId << " end group" << std::endl;
}

int eager_buffer_allocator_multithread_test() {
  // 1. 创建EagerBufferAllocator（使用默认内存分配器）
  auto defaultAllocator = BufferAllocator::Allocator::createDefault();
  EagerBufferAllocator allocator(defaultAllocator, 16, 0); // 16字节对齐

  // 2. 启动多线程前开启barrier（标记多线程阶段开始）
  allocator.barrierBegin();
  std::cout << "Main thread: barrier begin" << std::endl;

  // 3. 创建3个线程，每个线程在独立group中操作内存
  const int threadCount = 3;
  std::vector<std::thread> threads;
  std::vector<std::vector<MemChunk>> threadChunksList(
      threadCount); // 记录每个线程分配的内存

  for (int i = 0; i < threadCount; ++i) {
    threads.emplace_back(threadTask, &allocator, i,
                         std::ref(threadChunksList[i]));
  }

  // 4. 等待所有线程完成
  for (auto &t : threads) {
    t.join();
  }

  // 5. 结束barrier（合并所有线程组的空闲内存到全局）
  allocator.barrierEnd();
  std::cout << "Main thread: barrier end" << std::endl;

  // 6. 验证线程隔离：检查全局空闲列表是否可复用线程释放的内存
  std::cout << "\nVerifying global memory reuse after barrier..." << std::endl;
  size_t verifySize = 1024 * 2; // 尝试分配线程中释放过的2KB内存
  MemChunk globalChunk = allocator.alloc(verifySize);
  assert(!globalChunk.invalid() && "Global allocation failed");
  std::cout << "Global alloc: " << globalChunk.ptr() << " (size: " << verifySize
            << ")" << std::endl;

  // 7. 释放全局分配的内存
  allocator.free(globalChunk);
  std::cout << "Global free: " << globalChunk.ptr() << std::endl;

  // 8. 释放所有剩余内存
  allocator.release(true);
  std::cout << "\nAll memory released" << std::endl;

  return 0;
}

int eager_buffer_allocator_basic_test() {
  auto rawAlloc = BufferAllocator::Allocator::createDefault();
  std::shared_ptr<EagerBufferAllocator> mStaticAllocator;
  mStaticAllocator.reset(new EagerBufferAllocator(rawAlloc, 64, 160));
  int size = 100;
  MemChunk chunk = mStaticAllocator->alloc(size, false);
  mStaticAllocator->free(chunk);
  return 0;
}

int main() {
  // eager_buffer_allocator_basic_test();
  eager_buffer_allocator_multithread_test();

  // mStaticAllocator->sync();
  // std::cout << "E-Memory-Pool" << std::endl;
  // mStaticAllocatorRaw = mStaticAllocator;
  // auto mmapMem = BufferAllocator::Allocator::createMmap(
  //     hint().weightMemoryPath.c_str(), prefix.c_str(), "static", autoRemove);
  // size_t mmapSize = static_cast<size_t>(hint().mmapFileSize) * 1024 * 1024;
  // mStaticAllocator.reset(new EagerBufferAllocator(mmapMem, 32, mmapSize));
  // mStaticAllocatorMMap = mStaticAllocator;

  return 0;
}
