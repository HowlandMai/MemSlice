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

#include "memslice/allocator.hpp"
#include "memslice/pool.hpp"

using namespace memslice;
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

  std::printf("MemSlice 简易基准（%zu 次分配+释放，%zu 块同时存活）\n", kOps, kLive);
  std::printf("========================================================\n\n");

  // 本池
  MemoryPool<> pool;
  std::vector<void *> live(kLive, nullptr);
  for (auto &p: live)
    p = pool.Allocate(64);

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
  for (auto *p: live)
    pool.Deallocate(p);
  std::printf("  本池（分桶加锁）        : %6.2f ns/op\n", pool_ns);

  // glibc malloc
  std::vector<void *> live2(kLive, nullptr);
  for (auto &p: live2)
    p = std::malloc(64);
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
  for (auto *p: live2)
    std::free(p);
  std::printf("  glibc malloc/free      : %6.2f ns/op\n", malloc_ns);

  std::printf("\n  比值（本池 / malloc）   : %6.2f x\n", pool_ns / malloc_ns);

  // STL 容器路径：vector 通过 Allocator<T> 走池
  {
    std::printf("\n--- STL 容器路径（vector<int> 反复 push/pop）---\n");
    constexpr std::size_t kRounds = 2000;
    const auto t0 = Clock::now();
    for (std::size_t r = 0; r < kRounds; ++r) {
      std::vector<int, memslice::Allocator<int>> v;
      for (int i = 0; i < 64; ++i) {
        v.push_back(i);
      }
      g_sink = v.data();
    }
    const auto t1 = Clock::now();
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / kRounds;
    std::printf("  vector<int> 构造+填充+析构 : %6.2f us/轮\n", us);
    std::printf("  （说明：容器路径额外承担 Allocator 适配与 vector 自身开销，\n");
    std::printf("    与上面的裸分配数字不可直接比较。）\n");
  }
  std::printf("\n  说明：本池单线程下慢于 glibc malloc。原因与归因见\n");
  std::printf("        docs/04-performance-notes.md。\n");
  return 0;
}
