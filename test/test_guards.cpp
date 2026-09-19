// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 边界与防御性守卫：二级分配器越界请求、Reallocate 尺寸误报不再崩溃
// （DEF-001 回归）、无尺寸释放、空指针/零尺寸释放，以及调试期哨兵。
// 哨兵用例（sentinel_*）仅在启用 kDebugChecks 时真正执行，否则自我跳过。

#include "test_utils.hpp"

#include <cstddef>
#include <cstring>
#include <new>

#include "memory_pool/block.hpp"
#include "memory_pool/config.hpp"
#include "memory_pool/pool.hpp"
#include "memory_pool/second_level.hpp"

using namespace memory_pool;

// 测试二级分配器对越界请求的守卫（回归：此前发布构建下 free_list_ 越界读写）
MEMSLICE_CASE(guards, second_level_out_of_range) {
  SecondLevelAllocator<DefaultConfig> alloc;

  // 二级分配器以「底层块容量」为准
  MEMSLICE_EXPECT_THROWS(alloc.Allocate(DefaultConfig::kMaxSmallObjectBytes + 64), std::bad_alloc);
  MEMSLICE_INFO("out-of-range Allocate rejected with std::bad_alloc");

  // 边界值（恰好等于块上限）仍正常分配
  void *p = alloc.Allocate(DefaultConfig::kMaxSmallObjectBytes);
  MEMSLICE_EXPECT(p != nullptr);
  alloc.Deallocate(p, DefaultConfig::kMaxSmallObjectBytes);
  MEMSLICE_INFO("boundary-size Allocate still works");
}

// 回归（DEF-001）：旧实现中 Reallocate 会在旧尺寸 > 上限且新尺寸 > 上限时
// 对指针调用 glibc realloc。若该指针实际来自二级分配器，会把整个池 chunk 交给
// glibc 并释放，随后任何回收都构成 double free，进程 abort（ASan: attempting double-free）。
// 头部记账版以头部记录的旧尺寸为准，且全程只走「分配 + 拷贝 + 回收」，不会出现该路径。
MEMSLICE_CASE(guards, realloc_size_mismatch_survives) {
  MemoryPool<> pool;

  // 指针实际来自二级分配器（64 字节），旧实现只要调用方谎报旧尺寸就会崩溃
  void *p = pool.Allocate(64);
  MEMSLICE_EXPECT(p != nullptr);
  auto *bytes = static_cast<unsigned char *>(p);
  for (int i = 0; i < 64; ++i) {
    bytes[i] = static_cast<unsigned char>(i);
  }

  // 旧实现：一旦 new_size 与谎报的 old_size 同时 > 128，就会走 realloc(池内指针) -> 崩溃
  void *q = pool.Reallocate(p, 4096);
  MEMSLICE_EXPECT(q != nullptr);
  auto *qbytes = static_cast<unsigned char *>(q);
  bool preserved = true;
  for (int i = 0; i < 64; ++i) {
    if (qbytes[i] != static_cast<unsigned char>(i)) {
      preserved = false;
      break;
    }
  }
  MEMSLICE_EXPECT(preserved);
  MEMSLICE_INFO("Reallocate survived without trusting a caller-supplied old size (no realloc on pool pointer)");

  // 回收仍以头部为准，重复多轮也不会触发 double free
  for (int i = 0; i < 100; ++i) {
    void *r = pool.Allocate(64);
    MEMSLICE_EXPECT(r != nullptr);
    void *s = pool.Reallocate(r, 2048);
    MEMSLICE_EXPECT(s != nullptr);
    pool.Deallocate(s);
  }
  pool.Deallocate(q);
  MEMSLICE_INFO("100 alloc/realloc/free rounds completed without heap corruption");
}

// 测试释放侧不再依赖调用方尺寸：同一指针释放一次即正确入桶
MEMSLICE_CASE(guards, deallocate_needs_no_size) {
  MemoryPool<> pool;

  void *p = pool.Allocate(64);
  MEMSLICE_EXPECT(p != nullptr);
  pool.Deallocate(p); // 无需尺寸

  // 释放后该块应能被同尺寸请求复用（同一桶）
  void *q = pool.Allocate(64);
  MEMSLICE_EXPECT(q != nullptr);
  MEMSLICE_EXPECT(q == p);
  MEMSLICE_INFO("block reused at the same address after size-less deallocation: " << q);
  pool.Deallocate(q);
}

// ---- 调试期哨兵（仅在启用调试校验时生效）----
// 这些用例验证「静默的堆损坏」是否被转成可定位的显式失败。
// 发布构建下哨兵被编译掉，因此用 kDebugChecksConfig 作编译期开关跳过。

MEMSLICE_CASE(guards, sentinel_double_free) {
  if constexpr (!detail::kDebugChecksConfig<DefaultConfig>) {
    MEMSLICE_INFO("debug checks disabled in this build - case skipped");
    return;
  }
  MemoryPool<> pool;
  void *p = pool.Allocate(64);
  pool.Deallocate(p);
  // 第二次释放同一指针：调试构建下应被哨兵捕获（abort）
  MEMSLICE_EXPECT_DEATH(pool.Deallocate(p));
  MEMSLICE_INFO("double free detected by debug sentinel");
}

MEMSLICE_CASE(guards, sentinel_wild_pointer) {
  if constexpr (!detail::kDebugChecksConfig<DefaultConfig>) {
    MEMSLICE_INFO("debug checks disabled in this build - case skipped");
    return;
  }
  alignas(16) static unsigned char fake[256];
  std::memset(fake, 0xAB, sizeof(fake));
  MemoryPool<> pool;
  void *bogus = static_cast<void *>(fake + 64);
  // 释放一个从未由本池分配过的指针：应被哨兵捕获
  MEMSLICE_EXPECT_DEATH(pool.Deallocate(bogus));
  MEMSLICE_INFO("wild pointer detected by debug sentinel");
}

MEMSLICE_CASE(guards, sentinel_corrupt_header) {
  if constexpr (!detail::kDebugChecksConfig<DefaultConfig>) {
    MEMSLICE_INFO("debug checks disabled in this build - case skipped");
    return;
  }
  MemoryPool<> pool;
  void *p = pool.Allocate(64);
  // 模拟用户越界写：覆写用户指针前方的头部
  auto *hdr = reinterpret_cast<unsigned char *>(p) - detail::kHeaderSize;
  std::memset(hdr, 0x00, detail::kHeaderSize);
  MEMSLICE_EXPECT_DEATH(pool.Deallocate(p));
  MEMSLICE_INFO("corrupted header detected by debug sentinel");
}

// 对照：正常使用路径不得触发任何误报
MEMSLICE_CASE(guards, sentinel_no_false_positive) {
  MemoryPool<> pool;
  for (int i = 0; i < 500; ++i) {
    for (size_t s: {1u, 16u, 64u, 128u, 4096u}) {
      void *p = pool.Allocate(s);
      std::memset(p, 0x5A, s);
      pool.Deallocate(p);
    }
  }
  MEMSLICE_INFO("2500 alloc/free cycles across sizes: no false positive from the sentinel");
}

// 测试零尺寸指针的释放与空指针释放的健壮性
MEMSLICE_CASE(guards, deallocate_edge_cases) {
  MemoryPool<> pool;
  pool.Deallocate(nullptr); // 空指针应被安全忽略

  void *z = pool.Allocate(0);
  MEMSLICE_EXPECT(z != nullptr);
  pool.Deallocate(z);
  MEMSLICE_INFO("Deallocate(nullptr) and zero-size deallocation are both safe");
}
