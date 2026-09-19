// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 并发安全与线程安全开关。
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

// 测试并发安全（回归：默认开启线程安全，且无递归锁）
MEMSLICE_CASE(concurrency, thread_safety) {
  std::cout << "\nTesting thread safety..." << std::endl;

  constexpr int kThreads = 4;
  constexpr int kOps = 50000;

  // 共享同一个池，多个线程同时分配/释放
  MemoryPool<> pool;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&pool]() {
      for (int i = 0; i < kOps; ++i) {
        size_t size = (i % 64) + 1;
        void *p = pool.Allocate(size);
        pool.Deallocate(p, size);
      }
    });
  }
  for (auto &th: threads) {
    th.join();
  }

  std::cout << "Completed " << (kThreads * kOps) << " concurrent alloc/dealloc, "
            << "pool heap_size=" << pool.heap_size() << " bytes" << std::endl;
  assert(pool.heap_size() > 0);
  std::cout << "Thread safety test passed!" << std::endl;
}

// 测试关闭线程安全的配置路径（覆盖空操作锁的编译与运行）
struct NoThreadSafeConfig {
  static constexpr size_t kAlignSize = 8;
  static constexpr size_t kMaxSmallObjectBytes = 128;
  static constexpr int kDefaultNobjs = 20;
  static constexpr bool kThreadSafe = false;
};

MEMSLICE_CASE(concurrency, no_thread_safe_config) {
  std::cout << "\nTesting kThreadSafe=false configuration..." << std::endl;

  MemoryPool<NoThreadSafeConfig> pool;
  void *p = pool.Allocate(32);
  assert(p != nullptr);
  pool.Deallocate(p, 32);
  void *q = pool.Allocate(0);
  assert(q != nullptr);
  pool.Deallocate(q, 0);
  std::cout << "NoThreadSafe configuration test passed!" << std::endl;
}
