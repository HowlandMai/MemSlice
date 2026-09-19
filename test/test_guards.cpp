// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 边界与防御性守卫：二级分配器越界请求、错误尺寸释放。
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

// 测试二级分配器对越界请求的守卫（回归：此前发布构建下 free_list_ 越界读写）
MEMSLICE_CASE(guards, second_level_out_of_range) {
  std::cout << "\nTesting SecondLevelAllocator out-of-range guard..." << std::endl;

  SecondLevelAllocator<DefaultConfig> alloc;
  bool threw = false;
  try {
    (void) alloc.Allocate(DefaultConfig::kMaxSmallObjectBytes + 64);
  } catch (const std::bad_alloc &) {
    threw = true;
  }
  assert(threw);
  std::cout << "Out-of-range Allocate rejected with std::bad_alloc" << std::endl;

  // 边界值（恰好等于上限）仍正常分配
  void *p = alloc.Allocate(DefaultConfig::kMaxSmallObjectBytes);
  assert(p != nullptr);
  alloc.Deallocate(p, DefaultConfig::kMaxSmallObjectBytes);
  std::cout << "Boundary-size Allocate still works" << std::endl;

  std::cout << "SecondLevel out-of-range guard test passed!" << std::endl;
}

// 测试错误尺寸释放的防御行为（回归：不再静默越界分桶）
MEMSLICE_CASE(guards, wrong_size_deallocation) {
  std::cout << "\nTesting wrong-size deallocation (defensive)..." << std::endl;

  MemoryPool<> pool;
  void *p = pool.Allocate(64);
  assert(p != nullptr);
  // 用错误的尺寸释放（64 字节块被当 8 字节释放）：
  // 8 ≤ 上限 128，不触发越界断言；块按 8 字节进入 8B 桶（错桶）。
  // 该 64B 块从此只按 8B 复用（碎片化，属契约内的设计取舍），不会再按 64B 交出
  pool.Deallocate(p, 8);
  // 再从 64 字节桶分配：错放 8B 桶的块不可达，Refill 会取新块，不应拿到同一指针
  void *q = pool.Allocate(64);
  assert(q != nullptr);
  assert(q != p);
  std::cout << "Wrong-size deallocation survived; reallocated 64B at " << q << std::endl;
  pool.Deallocate(q, 64);

  std::cout << "Wrong-size deallocation test passed!" << std::endl;
}
