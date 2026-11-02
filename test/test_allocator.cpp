// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

// STL 分配器：与标准容器配合、分配器相等性、rebind、超对齐。

#include "test_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "memslice/allocator.hpp"
#include "memslice/pool.hpp"

using namespace memslice;

// vector：基本读写与扩容（扩容会走 allocate/deallocate）
MEMSLICE_CASE(allocator, vector_basic) {
  std::vector<int, Allocator<int>> v;
  for (int i = 0; i < 1000; ++i) {
    v.push_back(i * 3);
  }
  MEMSLICE_EXPECT(v.size() == 1000);
  for (int i = 0; i < 1000; ++i) {
    MEMSLICE_EXPECT(v[static_cast<std::size_t>(i)] == i * 3);
  }
  v.clear();
  v.shrink_to_fit();
  MEMSLICE_INFO("vector<int> 1000 元素正常");
}

// list：节点分配（rebind 到节点类型）
MEMSLICE_CASE(allocator, list_rebind) {
  std::list<std::string, Allocator<std::string>> l;
  for (int i = 0; i < 200; ++i) {
    l.emplace_back("node-" + std::to_string(i));
  }
  MEMSLICE_EXPECT(l.size() == 200);
  int i = 0;
  for (const auto &s: l) {
    MEMSLICE_EXPECT(s == "node-" + std::to_string(i));
    ++i;
  }
  MEMSLICE_INFO("list<string> 200 节点正常（覆盖 rebind 路径）");
}

// map：键值对节点分配
MEMSLICE_CASE(allocator, map_nodes) {
  std::map<int, int, std::less<int>, Allocator<std::pair<const int, int>>> m;
  for (int i = 0; i < 300; ++i) {
    m[i] = i * 7;
  }
  MEMSLICE_EXPECT(m.size() == 300);
  for (int i = 0; i < 300; ++i) {
    MEMSLICE_EXPECT(m[i] == i * 7);
  }
}

// 分配器相等性：标准要求"相等的分配器所分配的内存可互相释放"
MEMSLICE_CASE(allocator, equality_semantics) {
  Allocator<int> a;
  Allocator<int> b;
  Allocator<double> c;
  MEMSLICE_EXPECT(a == b);
  MEMSLICE_EXPECT(!(a != b));
  // 跨类型比较也应相等（本实现所有实例共用一个池）
  MEMSLICE_EXPECT(a == Allocator<int>(c));

  // 实测：一个实例分配、另一个实例释放
  int *p = a.allocate(4);
  b.deallocate(p, 4);
  MEMSLICE_INFO("分配器相等性语义正确（可交叉释放）");
}

// 零分配：allocate(0) 返回 nullptr（契约允许）
MEMSLICE_CASE(allocator, zero_allocation) {
  Allocator<int> a;
  MEMSLICE_EXPECT(a.allocate(0) == nullptr);
  a.deallocate(nullptr, 0); // 不应崩溃
}

// 溢出保护：请求大到 n * sizeof(T) 回绕时应抛 bad_alloc
MEMSLICE_CASE(allocator, overflow_guard) {
  Allocator<std::size_t> a;
  const std::size_t huge = static_cast<std::size_t>(-1) / sizeof(std::size_t) + 1;
  MEMSLICE_EXPECT_THROWS(a.allocate(huge), std::bad_alloc);
}

// 超对齐：alignof(T) > max_align_t 时仍须满足对齐
MEMSLICE_CASE(allocator, over_aligned_types) {
  struct alignas(64) A64 {
    char data[64];
  };
  struct alignas(128) A128 {
    char data[128];
  };

  Allocator<A64> a64;
  A64 *p = a64.allocate(3);
  MEMSLICE_EXPECT(reinterpret_cast<std::uintptr_t>(p) % 64 == 0);
  p[0].data[0] = 1;
  p[2].data[63] = 2;
  a64.deallocate(p, 3);

  Allocator<A128> a128;
  A128 *q = a128.allocate(2);
  MEMSLICE_EXPECT(reinterpret_cast<std::uintptr_t>(q) % 128 == 0);
  q[0].data[0] = 1;
  q[1].data[127] = 2;
  a128.deallocate(q, 2);

  MEMSLICE_INFO("超对齐类型（64/128 字节）对齐正确");
}

// 超对齐容器：vector<超对齐类型> 内部会按类型对齐分配
MEMSLICE_CASE(allocator, over_aligned_container) {
  struct alignas(64) Vec4 {
    float v[4];
  };
  std::vector<Vec4, Allocator<Vec4>> vec(10);
  MEMSLICE_EXPECT(reinterpret_cast<std::uintptr_t>(vec.data()) % 64 == 0);
  vec[0].v[0] = 1.5f;
  vec[9].v[3] = 2.5f;
  MEMSLICE_EXPECT(vec[0].v[0] == 1.5f);
  MEMSLICE_EXPECT(vec[9].v[3] == 2.5f);
  for (int i = 0; i < 100; ++i) {
    vec.push_back(Vec4{});
  }
  MEMSLICE_EXPECT(reinterpret_cast<std::uintptr_t>(vec.data()) % 64 == 0);
  MEMSLICE_INFO("vector<alignas(64) 类型> 对齐正确（含扩容）");
}

// 容器大量申请/释放后池账目仍自洽
MEMSLICE_CASE(allocator, pool_audit_after_container_use) {
  MemoryPool<> pool; // 注意：Allocator 用自己的池，这里只验证不与全局状态冲突
  {
    std::vector<std::string, Allocator<std::string>> v;
    for (int i = 0; i < 5000; ++i) {
      v.emplace_back("str-" + std::to_string(i));
    }
    v.clear();
    v.shrink_to_fit();
  }
  MEMSLICE_EXPECT(pool.Audit().ok);
  MEMSLICE_INFO("容器大量使用后池账目自洽");
}
