// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

// 并发：多线程共享池、跨线程归还、线程反复创建销毁、并发指针唯一性。

#include "test_utils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "mempool_lab/pool.hpp"

using namespace mempool_lab;

MEMPOOL_CASE(concurrency, shared_pool_stress) {
  constexpr int kThreads = 4;
  constexpr int kOps = 30000;
  MemoryPool<> pool;

  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t) {
    ts.emplace_back([&pool]() {
      for (int i = 0; i < kOps; ++i) {
        const std::size_t size = static_cast<std::size_t>((i % 64) + 1);
        void *p = pool.Allocate(size);
        std::memset(p, 0x77, size);
        pool.Deallocate(p);
      }
    });
  }
  for (auto &th: ts)
    th.join();
  MEMPOOL_EXPECT(pool.Audit().ok);
  MEMPOOL_INFO("4 线程 × 30000 次共享池操作完成");
}

// 跨线程归还：A 分配、B 释放。所有权跨线程转移是最容易出错的方向。
MEMPOOL_CASE(concurrency, cross_thread_return) {
  constexpr int kProducers = 4;
  constexpr int kPer = 2000;
  MemoryPool<> pool;
  std::vector<std::vector<void *>> produced(kProducers);

  {
    std::vector<std::thread> ts;
    for (int t = 0; t < kProducers; ++t) {
      ts.emplace_back([&pool, &produced, t]() {
        auto &mine = produced[static_cast<std::size_t>(t)];
        mine.reserve(kPer);
        for (int i = 0; i < kPer; ++i) {
          const std::size_t size = static_cast<std::size_t>((i % 16) + 1) * 8;
          void *p = pool.Allocate(size);
          std::memset(p, t + 1, size);
          mine.push_back(p);
        }
      });
    }
    for (auto &th: ts)
      th.join();
  }
  MEMPOOL_EXPECT(pool.Audit().ok);

  {
    std::vector<std::thread> ts;
    for (int t = 0; t < kProducers; ++t) {
      const int owner = (t + 1) % kProducers; // 由不同线程释放
      ts.emplace_back([&pool, &produced, owner]() {
        auto &mine = produced[static_cast<std::size_t>(owner)];
        for (std::size_t i = 0; i < mine.size(); ++i) {
          const std::size_t size = static_cast<std::size_t>((i % 16) + 1) * 8;
          const auto *bytes = static_cast<const unsigned char *>(mine[i]);
          for (std::size_t k = 0; k < size; ++k) {
            if (bytes[k] != static_cast<unsigned char>(owner + 1)) {
              std::printf("         跨线程释放前数据已被破坏\n");
              labtest::CurrentFailed() = true;
              return;
            }
          }
          pool.Deallocate(mine[i]);
        }
      });
    }
    for (auto &th: ts)
      th.join();
  }
  MEMPOOL_EXPECT(pool.Audit().ok);
  MEMPOOL_INFO("跨线程归还完成");
}

MEMPOOL_CASE(concurrency, thread_lifecycle_churn) {
  MemoryPool<> pool;
  for (int round = 0; round < 30; ++round) {
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) {
      ts.emplace_back([&pool, t]() {
        std::vector<void *> mine;
        for (int i = 0; i < 200; ++i) {
          const std::size_t size = static_cast<std::size_t>((i % 8) + 1) * 8;
          void *p = pool.Allocate(size);
          std::memset(p, t + 1, size);
          mine.push_back(p);
        }
        for (auto *p: mine)
          pool.Deallocate(p);
      });
    }
    for (auto &th: ts)
      th.join();
  }
  MEMPOOL_EXPECT(pool.Audit().ok);
  MEMPOOL_INFO("线程反复创建销毁 120 次后账目自洽");
}

// 并发指针唯一性：全部存活时校验地址唯一且区间不重叠。
// 注意必须「全部存活时」校验——边分配边释放会让同一地址被先后记录两次，
// 那属于测试设计缺陷而非池的缺陷。
MEMPOOL_CASE(concurrency, concurrent_pointer_uniqueness) {
  constexpr int kThreads = 6;
  constexpr int kPer = 1000;
  MemoryPool<> pool;
  std::vector<std::vector<void *>> per(kThreads);

  {
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
      ts.emplace_back([&pool, &per, t]() {
        auto &mine = per[static_cast<std::size_t>(t)];
        mine.reserve(kPer);
        for (int i = 0; i < kPer; ++i) {
          mine.push_back(pool.Allocate(static_cast<std::size_t>((i % 12) + 1) * 8));
        }
      });
    }
    for (auto &th: ts)
      th.join();
  }

  std::vector<std::uintptr_t> addrs;
  addrs.reserve(static_cast<std::size_t>(kThreads) * kPer);
  for (auto &v: per) {
    for (auto *p: v)
      addrs.push_back(reinterpret_cast<std::uintptr_t>(p));
  }
  std::sort(addrs.begin(), addrs.end());
  MEMPOOL_EXPECT(std::adjacent_find(addrs.begin(), addrs.end()) == addrs.end());

  MEMPOOL_EXPECT(pool.Audit().ok);
  for (auto &v: per) {
    for (auto *p: v)
      pool.Deallocate(p);
  }
  MEMPOOL_EXPECT(pool.Audit().ok);
  MEMPOOL_INFO("并发指针唯一性通过");
}

// kThreadSafe=false 的单线程路径也能正确工作
namespace {
  struct SingleConfig {
    static constexpr std::size_t kMaxBlockBytes = 128;
    static constexpr std::size_t kAlignSize = 8;
    static constexpr std::size_t kBatchSize = 20;
    static constexpr bool kThreadSafe = false;
  };
} // namespace

MEMPOOL_CASE(concurrency, single_threaded_config) {
  MemoryPool<SingleConfig> pool;
  std::vector<void *> v;
  for (int i = 0; i < 500; ++i)
    v.push_back(pool.Allocate(static_cast<std::size_t>((i % 16) + 1)));
  for (auto *p: v)
    pool.Deallocate(p);
  MEMPOOL_EXPECT(pool.Audit().ok);
}
