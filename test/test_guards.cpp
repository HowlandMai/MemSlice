// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 边界与防御性守卫：二级分配器越界请求、头部记账下的尺寸误用。

#include "test_utils.hpp"

#include <cstddef>
#include <new>

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
