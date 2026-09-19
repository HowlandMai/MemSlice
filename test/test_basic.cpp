// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 基础分配/释放、内存重用、自定义配置、零字节分配。

#include "test_utils.hpp"

#include <cstddef>
#include <cstdint>

#include "memory_pool/pool.hpp"

using namespace memory_pool;

// 测试基本的内存分配和释放功能
MEMSLICE_CASE(basic, allocate_deallocate) {
  MemoryPool<> pool;

  void *p1 = pool.Allocate(16);
  void *p2 = pool.Allocate(32);
  void *p3 = pool.Allocate(64);
  MEMSLICE_EXPECT(p1 != nullptr);
  MEMSLICE_EXPECT(p2 != nullptr);
  MEMSLICE_EXPECT(p3 != nullptr);
  MEMSLICE_INFO("allocated 16/32/64 bytes at " << p1 << " / " << p2 << " / " << p3);

  // 释放不再需要尺寸参数——记账由头部承担
  pool.Deallocate(p1);
  pool.Deallocate(p2);
  pool.Deallocate(p3);

  // 测试重复分配相同大小的内存（应该重用之前释放的内存）
  void *p4 = pool.Allocate(16);
  MEMSLICE_EXPECT(p4 != nullptr);
  MEMSLICE_INFO("reallocated 16 bytes at " << p4);
  pool.Deallocate(p4);

  // 测试大于配置的最大小对象大小的内存分配（应该使用一级分配器）
  void *p_large = pool.Allocate(2048);
  MEMSLICE_EXPECT(p_large != nullptr);
  MEMSLICE_INFO("allocated large memory (2048 bytes) at " << p_large);
  pool.Deallocate(p_large);
}

// 自定义配置结构体
struct CustomConfig {
  static constexpr size_t kAlignSize = 16; // 16 字节对齐
  static constexpr size_t kMaxSmallObjectBytes = 128; // 最大小对象 128 字节
  static constexpr int kDefaultNobjs = 50; // 默认每次分配 50 个对象
};

// 测试自定义配置的内存池
MEMSLICE_CASE(basic, custom_config) {
  MemoryPool<CustomConfig> pool;

  void *p1 = pool.Allocate(128); // 刚好是最大小对象大小
  MEMSLICE_EXPECT(p1 != nullptr);
  MEMSLICE_INFO("allocated 128 bytes (custom max size) at " << p1);
  pool.Deallocate(p1);
}

// 测试零字节分配（回归：此前会因索引无符号下溢导致越界崩溃）
MEMSLICE_CASE(basic, zero_size_allocation) {
  MemoryPool<> pool;
  void *a = pool.Allocate(0);
  void *b = pool.Allocate(0);
  MEMSLICE_EXPECT(a != nullptr);
  MEMSLICE_EXPECT(b != nullptr);
  MEMSLICE_EXPECT(a != b);
  MEMSLICE_INFO("allocated two distinct zero-size pointers: " << a << " vs " << b);
  pool.Deallocate(a);
  pool.Deallocate(b);
}

// 测试大量小内存分配
MEMSLICE_CASE(basic, large_number_of_allocations) {
  MemoryPool<> pool;
  const int kNumAllocations = 10000;
  std::vector<void *> pointers;
  pointers.reserve(kNumAllocations);

  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 32) + 1; // 1 到 32 字节
    void *p = pool.Allocate(size);
    MEMSLICE_EXPECT(p != nullptr);
    pointers.push_back(p);
  }

  for (int i = 0; i < kNumAllocations; ++i) {
    pool.Deallocate(pointers[i]);
  }
  MEMSLICE_INFO("allocated and freed " << kNumAllocations << " small blocks");
}
