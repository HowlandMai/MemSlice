// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 基础分配/释放、内存重用、自定义配置、零字节分配、大量小对象。
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

// 测试基本的内存分配和释放功能
MEMSLICE_CASE(basic, allocate_deallocate) {
  std::cout << "Testing basic allocate/deallocate..." << std::endl;

  // 使用默认配置的内存池
  MemoryPool<> pool;

  // 测试小内存分配
  void *p1 = pool.Allocate(16);
  assert(p1 != nullptr);
  std::cout << "Allocated 16 bytes at " << p1 << std::endl;

  void *p2 = pool.Allocate(32);
  assert(p2 != nullptr);
  std::cout << "Allocated 32 bytes at " << p2 << std::endl;

  void *p3 = pool.Allocate(64);
  assert(p3 != nullptr);
  std::cout << "Allocated 64 bytes at " << p3 << std::endl;

  // 测试释放内存
  pool.Deallocate(p1, 16);
  std::cout << "Deallocated 16 bytes at " << p1 << std::endl;

  pool.Deallocate(p2, 32);
  std::cout << "Deallocated 32 bytes at " << p2 << std::endl;

  pool.Deallocate(p3, 64);
  std::cout << "Deallocated 64 bytes at " << p3 << std::endl;

  // 测试重复分配相同大小的内存（应该重用之前释放的内存）
  void *p4 = pool.Allocate(16);
  assert(p4 != nullptr);
  std::cout << "Reallocated 16 bytes at " << p4 << std::endl;

  pool.Deallocate(p4, 16);

  // 测试大于配置的最大小对象大小的内存分配（应该使用一级分配器）
  void *p_large = pool.Allocate(2048);
  assert(p_large != nullptr);
  std::cout << "Allocated large memory (2048 bytes) at " << p_large << std::endl;

  pool.Deallocate(p_large, 2048);
  std::cout << "Deallocated large memory at " << p_large << std::endl;

  std::cout << "Basic allocate/deallocate test passed!" << std::endl;
}

// 自定义配置结构体
struct CustomConfig {
  static constexpr size_t kAlignSize = 16; // 16 字节对齐
  static constexpr size_t kMaxSmallObjectBytes = 128; // 最大小对象 128 字节
  static constexpr int kDefaultNobjs = 50; // 默认每次分配 50 个对象
};

// 测试自定义配置的内存池
MEMSLICE_CASE(basic, custom_config) {
  std::cout << "\nTesting custom configuration..." << std::endl;

  // 使用自定义配置的内存池
  MemoryPool<CustomConfig> pool;

  // 测试分配
  void *p1 = pool.Allocate(128); // 刚好是最大小对象大小
  assert(p1 != nullptr);
  std::cout << "Allocated 128 bytes (custom max size) at " << p1 << std::endl;

  pool.Deallocate(p1, 128);

  std::cout << "Custom configuration test passed!" << std::endl;
}

// 测试零字节分配（回归：此前会因索引无符号下溢导致越界崩溃）
MEMSLICE_CASE(basic, zero_size_allocation) {
  std::cout << "\nTesting zero-size allocation..." << std::endl;

  MemoryPool<> pool;
  void *a = pool.Allocate(0);
  void *b = pool.Allocate(0);
  assert(a != nullptr);
  assert(b != nullptr);
  assert(a != b);
  std::cout << "Allocated two distinct zero-size pointers: " << a << " vs " << b << std::endl;
  pool.Deallocate(a, 0);
  pool.Deallocate(b, 0);

  std::cout << "Zero-size allocation test passed!" << std::endl;
}

// 测试大量小内存分配
MEMSLICE_CASE(basic, large_number_of_allocations) {
  std::cout << "\nTesting large number of allocations..." << std::endl;

  MemoryPool<> pool;
  const int kNumAllocations = 10000;
  std::vector<void *> pointers;
  pointers.reserve(kNumAllocations);

  // 分配大量小内存
  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 32) + 1; // 1 到 32 字节
    void *p = pool.Allocate(size);
    pointers.push_back(p);
  }

  std::cout << "Allocated " << kNumAllocations << " small memory blocks" << std::endl;

  // 释放所有内存
  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 32) + 1;
    pool.Deallocate(pointers[i], size);
  }

  std::cout << "Freed all memory blocks" << std::endl;
  std::cout << "Large number of allocations test passed!" << std::endl;
}
