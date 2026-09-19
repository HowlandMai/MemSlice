// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 重新分配（Reallocate）：数据保全、跨级切换（小↔大）、反复增长的链条。

#include "test_utils.hpp"

#include <cstddef>

#include "memory_pool/pool.hpp"

using namespace memory_pool;

MEMSLICE_CASE(realloc, data_preservation) {
  MemoryPool<> pool;

  void *p1 = pool.Allocate(16);
  MEMSLICE_EXPECT(p1 != nullptr);
  int *data = static_cast<int *>(p1);
  *data = 12345;
  MEMSLICE_INFO("allocated 16 bytes at " << p1 << ", wrote 12345");

  // 重新分配为更大的内存（只需新尺寸，旧尺寸由头部记录）
  void *p2 = pool.Reallocate(p1, 32);
  MEMSLICE_EXPECT(p2 != nullptr);
  MEMSLICE_EXPECT(*static_cast<int *>(p2) == 12345);
  MEMSLICE_INFO("reallocated to 32 bytes at " << p2 << ", data preserved: " << *static_cast<int *>(p2));

  // 重新分配为更小的内存
  void *p3 = pool.Reallocate(p2, 8);
  MEMSLICE_EXPECT(p3 != nullptr);
  MEMSLICE_INFO("reallocated to 8 bytes at " << p3);
  pool.Deallocate(p3);
}

// 跨级切换：小对象 → 大对象 → 小对象，数据均应保全
MEMSLICE_CASE(realloc, cross_level_switch) {
  MemoryPool<> pool;

  void *p = pool.Allocate(64);
  MEMSLICE_EXPECT(p != nullptr);
  auto *as_bytes = static_cast<unsigned char *>(p);
  for (int i = 0; i < 64; ++i) {
    as_bytes[i] = static_cast<unsigned char>(i);
  }

  // 小 → 大（跨到一级分配器）
  void *big = pool.Reallocate(p, 4096);
  MEMSLICE_EXPECT(big != nullptr);
  auto *big_bytes = static_cast<unsigned char *>(big);
  bool preserved = true;
  for (int i = 0; i < 64; ++i) {
    if (big_bytes[i] != static_cast<unsigned char>(i)) {
      preserved = false;
      break;
    }
  }
  MEMSLICE_EXPECT(preserved);

  // 大 → 小（回到二级分配器）
  void *small = pool.Reallocate(big, 32);
  MEMSLICE_EXPECT(small != nullptr);
  MEMSLICE_EXPECT(static_cast<unsigned char *>(small)[0] == 0);
  MEMSLICE_EXPECT(static_cast<unsigned char *>(small)[31] == 31);
  MEMSLICE_INFO("cross-level reallocation preserved data (64 -> 4096 -> 32)");
  pool.Deallocate(small);
}

// 测试 Reallocate 的尺寸增长序列（反复增长，数据应始终保全）
MEMSLICE_CASE(realloc, growth_chain) {
  MemoryPool<> pool;
  auto *p = static_cast<unsigned char *>(pool.Allocate(8));
  MEMSLICE_EXPECT(p != nullptr);
  for (size_t i = 0; i < 8; ++i) {
    p[i] = static_cast<unsigned char>(i + 1);
  }

  for (size_t size: {16u, 64u, 129u, 1024u, 4096u, 32u}) {
    p = static_cast<unsigned char *>(pool.Reallocate(p, size));
    MEMSLICE_EXPECT(p != nullptr);
    // 前缀数据必须始终保全
    bool ok = true;
    for (size_t i = 0; i < 8; ++i) {
      if (p[i] != static_cast<unsigned char>(i + 1)) {
        ok = false;
        break;
      }
    }
    MEMSLICE_EXPECT(ok);
  }
  MEMSLICE_INFO("reallocation growth chain 8 -> 16 -> 64 -> 129 -> 1024 -> 4096 -> 32 preserved prefix");
  pool.Deallocate(p);
}
