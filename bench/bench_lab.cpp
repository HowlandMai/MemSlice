// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

// 简易基准：对比本池与 glibc malloc 的单次分配/释放成本。
//
// 说明：本基准**只断言自身有效性**，比值仅作信息输出——因为"池是否更快"
// 随平台、负载、编译选项变化。切莫把某台机器上的数字当成普适结论。

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "mempool_lab/pool.hpp"

using namespace mempool_lab;
using Clock = std::chrono::steady_clock;

namespace {

  volatile void *g_sink = nullptr;

  template<typename Fn>
  double TimePerOp(Fn &&fn, std::size_t ops) {
    const auto t0 = Clock::now();
    fn();
    const auto t1 = Clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / static_cast<double>(ops);
  }

} // namespace

int main() {
  constexpr std::size_t kOps = 200000;
  constexpr std::size_t kLive = 64;

  std::printf("mempool-lab 简易基准（%zu 次分配+释放，%zu 块同时存活）\n", kOps, kLive);
  std::printf("========================================================\n\n");

  // 本池
  MemoryPool<> pool;
  std::vector<void *> live(kLive, nullptr);
  for (auto &p: live) p = pool.Allocate(64);

  const double pool_ns = TimePerOp(
      [&] {
        for (std::size_t i = 0; i < kOps; ++i) {
          const std::size_t slot = i % kLive;
          pool.Deallocate(live[slot]);
          live[slot] = pool.Allocate(64);
          g_sink = live[slot];
        }
      },
      kOps);
  for (auto *p: live) pool.Deallocate(p);
  std::printf("  本池（分桶加锁）        : %6.2f ns/op\n", pool_ns);

  // glibc malloc
  std::vector<void *> live2(kLive, nullptr);
  for (auto &p: live2) p = std::malloc(64);
  const double malloc_ns = TimePerOp(
      [&] {
        for (std::size_t i = 0; i < kOps; ++i) {
          const std::size_t slot = i % kLive;
          std::free(live2[slot]);
          live2[slot] = std::malloc(64);
          g_sink = live2[slot];
        }
      },
      kOps);
  for (auto *p: live2) std::free(p);
  std::printf("  glibc malloc/free      : %6.2f ns/op\n", malloc_ns);

  std::printf("\n  比值（本池 / malloc）   : %6.2f x\n", pool_ns / malloc_ns);
  std::printf("\n  说明：本池单线程下慢于 glibc malloc。原因与归因见\n");
  std::printf("        docs/04-performance-notes.md。\n");
  return 0;
}
