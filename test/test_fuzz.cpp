// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

// 随机长跑与指针唯一性。
//
// 价值：普通单元测试只能覆盖想到的情况；随机序列能覆盖没想到的组合。
// 关键做法：**固定种子**——失败可复现，这是随机测试能被信任的前提。

#include "test_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "mempool_lab/pool.hpp"

using namespace mempool_lab;

namespace {

  // 确定性伪随机（xorshift32）：跨平台结果一致，便于复现
  struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) : s(seed == 0 ? 0x9E3779B9u : seed) {}
    std::uint32_t Next() {
      std::uint32_t x = s;
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      s = x;
      return x;
    }
    std::uint32_t Range(std::uint32_t lo, std::uint32_t hi) { return lo + (Next() % (hi - lo + 1)); }
  };

  struct Live {
    void *ptr = nullptr;
    std::size_t size = 0;
    unsigned char tag = 0;
  };

  void Fill(const Live &b) {
    auto *p = static_cast<unsigned char *>(b.ptr);
    for (std::size_t i = 0; i < b.size; ++i)
      p[i] = static_cast<unsigned char>(b.tag + i);
  }

  bool Intact(const Live &b) {
    const auto *p = static_cast<const unsigned char *>(b.ptr);
    for (std::size_t i = 0; i < b.size; ++i) {
      if (p[i] != static_cast<unsigned char>(b.tag + i))
        return false;
    }
    return true;
  }

  // 存活块地址区间两两不重叠 —— 能抓「同一块被交出两次」
  bool Disjoint(const std::vector<Live> &v) {
    for (std::size_t i = 0; i < v.size(); ++i) {
      const auto a0 = reinterpret_cast<std::uintptr_t>(v[i].ptr);
      const auto a1 = a0 + v[i].size;
      for (std::size_t j = i + 1; j < v.size(); ++j) {
        const auto b0 = reinterpret_cast<std::uintptr_t>(v[j].ptr);
        const auto b1 = b0 + v[j].size;
        if (a0 < b1 && b0 < a1)
          return false;
      }
    }
    return true;
  }

} // namespace

MEMPOOL_CASE(fuzz, randomized_long_run) {
  MemoryPool<> pool;
  Rng rng(20260920);
  std::vector<Live> live;
  live.reserve(256);

  for (int round = 0; round < 3000; ++round) {
    const std::uint32_t action = rng.Range(0, 99);

    if (action < 55 || live.empty()) {
      Live b;
      b.size = rng.Range(1, 600);
      b.tag = static_cast<unsigned char>(rng.Range(1, 200));
      b.ptr = pool.Allocate(b.size);
      MEMPOOL_EXPECT(b.ptr != nullptr);
      Fill(b);
      live.push_back(b);
    } else if (action < 85) {
      const std::size_t i = rng.Range(0, static_cast<std::uint32_t>(live.size() - 1));
      MEMPOOL_EXPECT(Intact(live[i]));
      pool.Deallocate(live[i].ptr);
      live[i] = live.back();
      live.pop_back();
    } else {
      const std::size_t i = rng.Range(0, static_cast<std::uint32_t>(live.size() - 1));
      MEMPOOL_EXPECT(Intact(live[i]));
      const std::size_t ns = rng.Range(1, 600);
      const std::size_t keep = (ns < live[i].size) ? ns : live[i].size;
      void *np = pool.Reallocate(live[i].ptr, ns);
      MEMPOOL_EXPECT(np != nullptr);
      const auto *src = static_cast<const unsigned char *>(np);
      for (std::size_t k = 0; k < keep; ++k) {
        MEMPOOL_EXPECT(src[k] == static_cast<unsigned char>(live[i].tag + k));
      }
      live[i].ptr = np;
      live[i].size = ns;
      live[i].tag = static_cast<unsigned char>(rng.Range(1, 200));
      Fill(live[i]);
    }

    // 每轮校验全部不变量
    MEMPOOL_EXPECT(pool.Audit().ok);
    for (const auto &b: live)
      MEMPOOL_EXPECT(Intact(b));
    MEMPOOL_EXPECT(Disjoint(live));
  }

  for (const auto &b: live) {
    MEMPOOL_EXPECT(Intact(b));
    pool.Deallocate(b.ptr);
  }
  MEMPOOL_EXPECT(pool.Audit().ok);
  MEMPOOL_INFO("随机长跑 3000 轮完成");
}

MEMPOOL_CASE(fuzz, same_size_churn) {
  MemoryPool<> pool;
  std::vector<void *> v;
  v.reserve(100);
  for (int round = 0; round < 200; ++round) {
    for (int i = 0; i < 100; ++i)
      v.push_back(pool.Allocate(64));
    // 存活期间两两不同
    for (std::size_t i = 0; i < v.size(); ++i) {
      for (std::size_t j = i + 1; j < v.size(); ++j)
        MEMPOOL_EXPECT(v[i] != v[j]);
    }
    for (auto *p: v)
      pool.Deallocate(p);
    v.clear();
  }
  MEMPOOL_EXPECT(pool.Audit().ok);
}
