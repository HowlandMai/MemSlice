// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

// 审计的两面性：正常负载下不误报，注入错误时必须报出。
//
// 只测"正常时不误报"是不够的——那样一个永远返回 ok 的审计也能通过。
// 必须同时证明它能抓到错，否则审计只是装饰。

#include "test_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "memslice/audit.hpp"
#include "memslice/block.hpp"
#include "memslice/pool.hpp"

using namespace memslice;

// ---- 防误报：正常负载下审计必须通过 ----

MEMSLICE_CASE(audit, healthy_interleaved) {
  MemoryPool<> pool;
  std::vector<void *> v;
  for (int i = 0; i < 2000; ++i)
    v.push_back(pool.Allocate(static_cast<std::size_t>((i % 32) + 1)));
  MEMSLICE_EXPECT(pool.Audit().ok);
  for (auto *p: v)
    pool.Deallocate(p);
  MEMSLICE_EXPECT(pool.Audit().ok);
}

MEMSLICE_CASE(audit, healthy_mixed_sizes_and_system) {
  MemoryPool<> pool;
  std::vector<void *> small, big;
  for (int i = 0; i < 200; ++i) {
    small.push_back(pool.Allocate(48));
    big.push_back(pool.Allocate(4096)); // 走系统分配
  }
  MEMSLICE_EXPECT(pool.Audit().ok);
  for (auto *p: small)
    pool.Deallocate(p);
  for (auto *p: big)
    pool.Deallocate(p);
  MEMSLICE_EXPECT(pool.Audit().ok);
}

// 自定义配置必须放在命名空间作用域——局部类不允许静态数据成员
namespace {
  struct SmallConfig {
    static constexpr std::size_t kMaxBlockBytes = 64;
    static constexpr std::size_t kAlignSize = 8;
    static constexpr std::size_t kBatchSize = 8;
    static constexpr bool kThreadSafe = true;
  };
} // namespace

MEMSLICE_CASE(audit, healthy_custom_config) {
  MemoryPool<SmallConfig> pool;
  std::vector<void *> v;
  for (int i = 0; i < 500; ++i)
    v.push_back(pool.Allocate(static_cast<std::size_t>((i % 8) + 1)));
  MEMSLICE_EXPECT(pool.Audit().ok);
  for (auto *p: v)
    pool.Deallocate(p);
  MEMSLICE_EXPECT(pool.Audit().ok);
}

// ---- 防漏报：注入错误必须被抓到 ----

MEMSLICE_CASE(audit, detects_cycle) {
  alignas(8) static unsigned char buf[256];
  auto *n0 = reinterpret_cast<FreeNode *>(buf);
  auto *n1 = reinterpret_cast<FreeNode *>(buf + 64);
  n0->next = n1;
  n1->next = n0; // 成环
  const auto info = detail::InspectChain(n0, 8, 1000);
  MEMSLICE_EXPECT(info.cyclic);
}

MEMSLICE_CASE(audit, self_loop_terminates) {
  // 回归：审计必须先查环再遍历，否则遇到自环会挂死
  alignas(8) static unsigned char buf[64];
  auto *n0 = reinterpret_cast<FreeNode *>(buf);
  n0->next = n0;
  const auto info = detail::InspectChain(n0, 8, 10);
  MEMSLICE_EXPECT(info.cyclic);
  MEMSLICE_EXPECT(info.length == 0);
}

MEMSLICE_CASE(audit, detects_misalignment) {
  alignas(8) static unsigned char buf[64];
  auto *n0 = reinterpret_cast<FreeNode *>(buf + 4); // 故意错位
  n0->next = nullptr;
  MEMSLICE_EXPECT(detail::InspectChain(n0, 8, 1000).misaligned);
}

MEMSLICE_CASE(audit, detects_overlong_chain) {
  std::vector<FreeNode> nodes(500);
  for (std::size_t i = 0; i + 1 < nodes.size(); ++i)
    nodes[i].next = &nodes[i + 1];
  nodes.back().next = nullptr;
  MEMSLICE_EXPECT(detail::InspectChain(nodes.data(), 8, 100).overlong);
}

MEMSLICE_CASE(audit, allocated_block_header_is_consistent) {
  MemoryPool<> pool;
  for (std::size_t s = 1; s <= 600; ++s) {
    void *p = pool.Allocate(s);
    const char *err = pool.VerifyAllocated(p, s);
    MEMSLICE_EXPECT(err == nullptr);
    pool.Deallocate(p);
  }
}

// 固化一条结构性事实：空闲节点的头部会被 next 覆盖。
// 因此「在空闲链表上读头部核对容量」是做不到的——写审计时不要尝试。
MEMSLICE_CASE(audit, free_node_header_is_overwritten) {
  MemoryPool<> pool;
  void *p = pool.Allocate(64);
  const auto *h = detail::HeaderOf(p);
  const auto head_addr = reinterpret_cast<std::uintptr_t>(h);
  pool.Deallocate(p);
  const auto *after = reinterpret_cast<const detail::Header *>(head_addr);
  // 释放后该处被写成链表指针，不再是原来的 base
  MEMSLICE_EXPECT(reinterpret_cast<std::uintptr_t>(after->base) != head_addr);
}
