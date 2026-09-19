// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 性能基准（内存池 vs 系统 malloc）。
//
// 关于断言的取舍：这里**不**断言「池一定更快」。实测表明现代 glibc 在
// 固定小对象的 malloc+free 上极快（tcache 命中，循环甚至会被优化器整体消除），
// 因此任何「池更快」的断言在本平台都不成立，写进测试只会变成假绿或长期红灯。
//
// 本文件断言的是**基准自身的有效性**，即：
//   - 每次操作耗时必须为有限正数（防止循环被优化掉导致 0 ns 的假绿）；
//   - 分配出去的指针必须互不相同、且池的堆用量确实增长（证明真的在分配）；
//   - 基准本身不能把被测代码优化掉（用 volatile sink 强制保活）。
// 比值以信息形式输出，供人工判读，不做断言。
//
// 单独运行：xmake run bench_memory_pool

#include "test_utils.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "memory_pool/allocator.hpp"
#include "memory_pool/pool.hpp"

using namespace memory_pool;

namespace {

  using Clock = std::chrono::steady_clock;

  // volatile 汇聚点：阻止优化器把「分配 + 立即释放」整段消除
  volatile void *g_sink = nullptr;

  // 测量一段可重复执行的代码的耗时（纳秒）。取多次运行的最小值，
  // 最小值比平均值更接近真实下限，可减少调度抖动带来的噪声。
  template<typename Fn>
  double MeasureNs(int rounds, Fn &&fn) {
    double best = 1e300;
    for (int r = 0; r < rounds; ++r) {
      const auto start = Clock::now();
      fn();
      const auto end = Clock::now();
      const double ns = std::chrono::duration<double, std::nano>(end - start).count();
      if (ns < best) {
        best = ns;
      }
    }
    return best;
  }

} // namespace

// 固定尺寸高频分配/释放——内存池最有利的场景（分桶命中率最高）
MEMSLICE_CASE(perf, fixed_size_high_frequency) {
  constexpr int kIterations = 200000;
  constexpr size_t kFixedSize = 32;

  MemoryPool<> pool;

  // 预热：让池预先建好 32 字节桶的空闲链表，避免把首次 chunk 申请计入
  for (int i = 0; i < 1000; ++i) {
    pool.Deallocate(pool.Allocate(kFixedSize));
  }

  const double pool_ns = MeasureNs(5, [&pool] {
    for (int i = 0; i < kIterations; ++i) {
      void *p = pool.Allocate(kFixedSize);
      g_sink = p;
      pool.Deallocate(p);
    }
  });

  const double malloc_ns = MeasureNs(5, [] {
    for (int i = 0; i < kIterations; ++i) {
      void *p = std::malloc(kFixedSize);
      g_sink = p;
      std::free(p);
    }
  });

  const double pool_ns_per_op = pool_ns / kIterations;
  const double malloc_ns_per_op = malloc_ns / kIterations;

  MEMSLICE_INFO("fixed 32B x " << kIterations << ": pool=" << pool_ns_per_op << " ns/op, malloc=" << malloc_ns_per_op
                               << " ns/op, ratio=" << (pool_ns_per_op / malloc_ns_per_op));

  // 有效性断言
  MEMSLICE_EXPECT(pool_ns_per_op > 0.0);
  MEMSLICE_EXPECT(malloc_ns_per_op > 0.0);
  MEMSLICE_EXPECT(pool.heap_size() > 0);
}

// 混排尺寸——分桶命中率下降的场景，与上一用例对比用
MEMSLICE_CASE(perf, mixed_size) {
  constexpr int kIterations = 200000;

  MemoryPool<> pool;

  const double pool_ns = MeasureNs(5, [&pool] {
    for (int i = 0; i < kIterations; ++i) {
      size_t size = (i % 64) + 1;
      void *p = pool.Allocate(size);
      g_sink = p;
      pool.Deallocate(p);
    }
  });

  const double malloc_ns = MeasureNs(5, [] {
    for (int i = 0; i < kIterations; ++i) {
      size_t size = (i % 64) + 1;
      void *p = std::malloc(size);
      g_sink = p;
      std::free(p);
    }
  });

  MEMSLICE_INFO("mixed 1..64B x " << kIterations << ": pool=" << (pool_ns / kIterations)
                                  << " ns/op, malloc=" << (malloc_ns / kIterations) << " ns/op");

  MEMSLICE_EXPECT(pool_ns / kIterations > 0.0);
  MEMSLICE_EXPECT(malloc_ns / kIterations > 0.0);
}

// 批量存活（先分配 N 个再统一释放）——检验高水位下的分配吞吐与指针唯一性
MEMSLICE_CASE(perf, batch_survive_then_free) {
  constexpr int kBatch = 50000;
  constexpr size_t kSize = 64;

  MemoryPool<> pool;
  std::vector<void *> live;
  live.reserve(kBatch);

  for (int i = 0; i < kBatch; ++i) {
    live.push_back(pool.Allocate(kSize));
  }
  MEMSLICE_EXPECT(live.size() == static_cast<size_t>(kBatch));

  // 全部指针必须互不相同（池不得把同一块交两次）
  for (size_t i = 0; i < live.size(); ++i) {
    MEMSLICE_EXPECT(live[i] != nullptr);
    for (size_t j = i + 1; j < live.size(); ++j) {
      if (live[i] == live[j]) {
        MEMSLICE_FAIL("pool handed out the same block twice");
      }
    }
  }

  for (void *p: live) {
    pool.Deallocate(p);
  }
  MEMSLICE_INFO("batch " << kBatch << " x " << kSize
                         << "B live simultaneously, then freed; heap_size=" << pool.heap_size());
}

// STL 容器分配器路径（Allocator<T> 经全局池）
MEMSLICE_CASE(perf, stl_allocator_path) {
  constexpr int kPushBacks = 20000;

  const double pool_ns = MeasureNs(5, [] {
    std::vector<int, Allocator<int>> v;
    for (int i = 0; i < kPushBacks; ++i) {
      v.push_back(i);
    }
    g_sink = v.data();
  });

  const double std_ns = MeasureNs(5, [] {
    std::vector<int> v;
    for (int i = 0; i < kPushBacks; ++i) {
      v.push_back(i);
    }
    g_sink = v.data();
  });

  MEMSLICE_INFO("vector<int> push_back x " << kPushBacks << ": pool allocator=" << pool_ns / kPushBacks
                                           << " ns/op, std=" << std_ns / kPushBacks << " ns/op");

  MEMSLICE_EXPECT(pool_ns > 0.0);
  MEMSLICE_EXPECT(std_ns > 0.0);
}
