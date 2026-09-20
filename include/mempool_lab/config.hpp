// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

#ifndef MEMPOOL_LAB_CONFIG_HPP_
#define MEMPOOL_LAB_CONFIG_HPP_

#include <cstddef>

namespace mempool_lab {

  // 池的配置参数。
  //
  // 用模板参数而非宏：不同配置的池可以在同一程序里共存（例如测试里同时验证
  // 默认配置与自定义配置），且配置错误能在编译期发现。
  struct DefaultConfig {
    // 二级分配器管理的最大字节数（底层块容量，含头部）。
    // 超过此值的请求交给一级分配器（直接 malloc）。
    static constexpr std::size_t kMaxBlockBytes = 128;

    // 块的对齐粒度。所有块容量都会向上取整到它的整数倍。
    // 必须是 2 的幂。
    static constexpr std::size_t kAlignSize = 8;

    // 二级分配器向系统批量申请时，一次切分出多少个块。
    // 越大则单次分配越省系统调用，但初始浪费越多。
    static constexpr std::size_t kBatchSize = 20;

    // 是否启用线程安全（分桶加锁）。关闭后单线程零锁开销。
    static constexpr bool kThreadSafe = true;
  };

  namespace detail {

    template<typename C>
    constexpr bool ValidConfig() {
      static_assert(C::kAlignSize > 0, "对齐粒度必须为正");
      static_assert((C::kAlignSize & (C::kAlignSize - 1)) == 0, "对齐粒度必须是 2 的幂");
      static_assert(C::kMaxBlockBytes > 0, "最大块容量必须为正");
      static_assert(C::kMaxBlockBytes % C::kAlignSize == 0, "最大块容量必须是对齐粒度的整数倍");
      static_assert(C::kBatchSize > 0, "批量大小必须为正");
      return true;
    }

  } // namespace detail

} // namespace mempool_lab

#endif // MEMPOOL_LAB_CONFIG_HPP_
