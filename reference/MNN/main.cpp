#include <iostream>

#include "core/BufferAllocator.hpp"

using namespace MNN;

int main() {
  auto rawAlloc = BufferAllocator::Allocator::createDefault();
  std::shared_ptr<EagerBufferAllocator> mStaticAllocator;
  mStaticAllocator.reset(new EagerBufferAllocator(rawAlloc));
  int size = 100;
  MemChunk chunk = mStaticAllocator->alloc(size, false);
  mStaticAllocator->sync();
  std::cout << "E-Memory-Pool" << std::endl;

  // mStaticAllocatorRaw = mStaticAllocator;
  // auto mmapMem = BufferAllocator::Allocator::createMmap(
  //     hint().weightMemoryPath.c_str(), prefix.c_str(), "static", autoRemove);
  // size_t mmapSize = static_cast<size_t>(hint().mmapFileSize) * 1024 * 1024;
  // mStaticAllocator.reset(new EagerBufferAllocator(mmapMem, 32, mmapSize));
  // mStaticAllocatorMMap = mStaticAllocator;

  return 0;
}
