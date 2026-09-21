// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

// 基础功能：分配/释放、地址重用、边界尺寸、零字节、用户数据完整性、对齐。

#include "test_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "mempool_lab/pool.hpp"

using namespace mempool_lab;

MEMPOOL_CASE(basic, allocate_and_free) {
  MemoryPool<> pool;
  void *p = pool.Allocate(64);
  MEMPOOL_EXPECT(p != nullptr);
  std::memset(p, 0xAB, 64);
  pool.Deallocate(p);
  MEMPOOL_INFO("单次分配/释放正常");
}

// 释放后内存应被复用：同尺寸的分配应拿到同一地址（空闲链表是 LIFO）
MEMPOOL_CASE(basic, memory_is_reused) {
  MemoryPool<> pool;
  void *a = pool.Allocate(64);
  pool.Deallocate(a);
  void *b = pool.Allocate(64);
  MEMPOOL_EXPECT(a == b);
  MEMPOOL_INFO("释放后同尺寸复用同一地址（LIFO 空闲链表）");
}

// 零字节分配：必须返回可释放的有效指针，且两次分配不得是同一指针
MEMPOOL_CASE(basic, zero_size) {
  MemoryPool<> pool;
  void *a = pool.Allocate(0);
  void *b = pool.Allocate(0);
  MEMPOOL_EXPECT(a != nullptr);
  MEMPOOL_EXPECT(b != nullptr);
  MEMPOOL_EXPECT(a != b);
  pool.Deallocate(a);
  pool.Deallocate(b);
}

// 边界尺寸：最小、对齐边界、池上限、刚越上限（转向系统分配）
MEMPOOL_CASE(basic, boundary_sizes) {
  MemoryPool<> pool;
  const std::size_t sizes[] = {0, 1, 7, 8, 9, 63, 64, 65, 95, 96, 97, 112, 113, 500, 4096};
  for (std::size_t s: sizes) {
    void *p = pool.Allocate(s);
    MEMPOOL_EXPECT(p != nullptr);
    if (s > 0) {
      std::memset(p, 0x5A, s);
    }
    MEMPOOL_EXPECT(pool.VerifyAllocated(p, s) == nullptr);
    pool.Deallocate(p);
  }
  MEMPOOL_EXPECT(pool.Audit().ok);
}

// 用户数据完整性：头部不得侵占或错位用户数据
MEMPOOL_CASE(basic, payload_integrity) {
  MemoryPool<> pool;
  for (std::size_t s = 1; s <= 600; ++s) {
    auto *p = static_cast<unsigned char *>(pool.Allocate(s));
    for (std::size_t i = 0; i < s; ++i) {
      p[i] = static_cast<unsigned char>(i * 31 + 7);
    }
    for (std::size_t i = 0; i < s; ++i) {
      MEMPOOL_EXPECT(p[i] == static_cast<unsigned char>(i * 31 + 7));
    }
    pool.Deallocate(p);
  }
}

// 对齐：返回的指针必须满足 max_align_t
MEMPOOL_CASE(basic, alignment) {
  MemoryPool<> pool;
  for (std::size_t s: {1u, 7u, 8u, 63u, 64u, 200u, 1000u}) {
    void *p = pool.Allocate(s);
    MEMPOOL_EXPECT(reinterpret_cast<std::uintptr_t>(p) % alignof(std::max_align_t) == 0);
    pool.Deallocate(p);
  }
}

// 大量小对象：逼近多次 chunk 扩张
MEMPOOL_CASE(basic, many_small_objects) {
  MemoryPool<> pool;
  std::vector<void *> v;
  v.reserve(20000);
  for (int i = 0; i < 20000; ++i) {
    v.push_back(pool.Allocate(static_cast<std::size_t>((i % 16) + 1)));
  }
  MEMPOOL_EXPECT(pool.Audit().ok);
  for (auto *p: v) {
    pool.Deallocate(p);
  }
  MEMPOOL_EXPECT(pool.Audit().ok);
}

// 超大尺寸：走系统分配路径
MEMPOOL_CASE(basic, oversized_uses_system) {
  MemoryPool<> pool;
  void *p = pool.Allocate(100000);
  MEMPOOL_EXPECT(p != nullptr);
  MEMPOOL_EXPECT(detail::SourceOf(p) == detail::BlockSource::kSystem);
  std::memset(p, 0x11, 100000);
  pool.Deallocate(p);
}

// 远超可表达范围的请求应抛异常
MEMPOOL_CASE(basic, rejects_unrepresentable) {
  MemoryPool<> pool;
  MEMPOOL_EXPECT_THROWS(pool.Allocate(MemoryPool<>::kMaxRequestBytes + 1), std::bad_alloc);
}

// 头部尺寸必须是 max_align_t 的整数倍（这条约束曾导致过段错误）
MEMPOOL_CASE(basic, header_geometry) {
  MEMPOOL_EXPECT(detail::kHeaderBytes % alignof(std::max_align_t) == 0);
  MEMPOOL_EXPECT(detail::kHeaderBytes >= sizeof(detail::Header));
}
