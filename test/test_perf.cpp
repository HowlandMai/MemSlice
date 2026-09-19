// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 性能对比（内存池 vs 系统 malloc）。仅编入 bench_memory_pool。
#include "test_utils.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <list>
#include <string>
#include <thread>
#include <vector>

#include "./include/memory_pool.hpp"

using namespace memory_pool;

// 测试内存池的性能
MEMSLICE_CASE(perf, pool_vs_malloc) {
  std::cout << "\nTesting performance..." << std::endl;

  const int kNumAllocations = 100000;

  // 测试内存池性能
  MemoryPool<> pool;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 64) + 1; // 1 到 64 字节
    void *p = pool.Allocate(size);
    pool.Deallocate(p, size);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto pool_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "Memory pool: " << pool_duration.count() << " ms for " << kNumAllocations << " allocations/deallocations"
            << std::endl;

  // 测试系统 malloc 性能
  start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 64) + 1; // 1 到 64 字节
    void *p = std::malloc(size);
    std::free(p);
  }

  end = std::chrono::high_resolution_clock::now();
  auto malloc_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "System malloc: " << malloc_duration.count() << " ms for " << kNumAllocations
            << " allocations/deallocations" << std::endl;

  std::cout << "Performance test completed!" << std::endl;
}
