// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 重新分配（Reallocate）的数据保全。
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

// 测试内存重新分配功能
MEMSLICE_CASE(realloc, data_preservation) {
  std::cout << "\nTesting reallocate..." << std::endl;

  MemoryPool<> pool;

  // 分配初始内存
  void *p1 = pool.Allocate(16);
  assert(p1 != nullptr);
  std::cout << "Allocated 16 bytes at " << p1 << std::endl;

  // 向 p1 写入一些数据
  int *data = static_cast<int *>(p1);
  *data = 12345;

  // 重新分配为更大的内存
  void *p2 = pool.Reallocate(p1, 16, 32);
  assert(p2 != nullptr);
  std::cout << "Reallocated to 32 bytes at " << p2 << std::endl;

  // 验证数据是否被正确拷贝
  assert(*static_cast<int *>(p2) == 12345);
  std::cout << "Data preserved after reallocation: " << *static_cast<int *>(p2) << std::endl;

  // 重新分配为更小的内存
  void *p3 = pool.Reallocate(p2, 32, 8);
  assert(p3 != nullptr);
  std::cout << "Reallocated to 8 bytes at " << p3 << std::endl;

  pool.Deallocate(p3, 8);

  std::cout << "Reallocate test passed!" << std::endl;
}
