// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

// 重新分配：数据保全、同容量原地复用、跨级切换。

#include "test_utils.hpp"

#include <cstddef>
#include <cstring>

#include "mempool_lab/pool.hpp"

using namespace mempool_lab;

MEMPOOL_CASE(realloc, preserves_data_on_grow) {
  MemoryPool<> pool;
  void *p = pool.Allocate(32);
  for (int i = 0; i < 32; ++i)
    static_cast<char *>(p)[i] = static_cast<char>(i);
  void *q = pool.Reallocate(p, 5000);
  MEMPOOL_EXPECT(q != nullptr);
  for (int i = 0; i < 32; ++i)
    MEMPOOL_EXPECT(static_cast<char *>(q)[i] == static_cast<char>(i));
  pool.Deallocate(q);
}

MEMPOOL_CASE(realloc, in_place_when_same_capacity) {
  MemoryPool<> pool;
  void *p = pool.Allocate(64);
  const std::size_t cap = detail::CapacityOf(p);
  const std::size_t usable = cap - detail::kHeaderBytes;
  const std::size_t before = pool.total_bytes();
  for (std::size_t n = 1; n <= usable; ++n) {
    void *q = pool.Reallocate(p, n);
    MEMPOOL_EXPECT(q == p); // 同容量内应原地复用
  }
  MEMPOOL_EXPECT(pool.total_bytes() == before); // 且不应产生新的系统申请
  pool.Deallocate(p);
}

MEMPOOL_CASE(realloc, moves_when_capacity_exceeded) {
  MemoryPool<> pool;
  void *p = pool.Allocate(64);
  const std::size_t usable = detail::CapacityOf(p) - detail::kHeaderBytes;
  void *q = pool.Reallocate(p, usable + 1);
  MEMPOOL_EXPECT(q != p);
  pool.Deallocate(q);
}

MEMPOOL_CASE(realloc, null_pointer_allocates) {
  MemoryPool<> pool;
  void *p = pool.Reallocate(nullptr, 128);
  MEMPOOL_EXPECT(p != nullptr);
  pool.Deallocate(p);
}
