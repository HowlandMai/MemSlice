// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 性能对比（内存池 vs 系统 malloc）。默认不编入主测试可执行文件，
// 用 `xmake run bench_memory_pool` 单独运行。

#include "test_utils.hpp"

#include <chrono>
#include <cstddef>
#include <cstdlib>

#include "memory_pool/pool.hpp"

using namespace memory_pool;

MEMSLICE_CASE(perf, pool_vs_malloc) {
  const int kNumAllocations = 100000;

  // 测试内存池性能
  MemoryPool<> pool;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 64) + 1; // 1 到 64 字节
    void *p = pool.Allocate(size);
    pool.Deallocate(p);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto pool_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  MEMSLICE_INFO("memory pool: " << pool_duration.count() << " ms");

  // 测试系统 malloc 性能
  start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 64) + 1; // 1 到 64 字节
    void *p = std::malloc(size);
    std::free(p);
  }

  end = std::chrono::high_resolution_clock::now();
  auto malloc_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  MEMSLICE_INFO("system malloc: " << malloc_duration.count() << " ms");

  // 仅作参考输出，不作性能断言（不同环境结论不同）
  MEMSLICE_INFO("for " << kNumAllocations << " alloc/dealloc pairs");
}
