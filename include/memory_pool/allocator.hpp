// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_ALLOCATOR_HPP_
#define MEMORY_POOL_MEMORY_POOL_ALLOCATOR_HPP_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>

#include "block.hpp"
#include "config.hpp"
#include "pool.hpp"

namespace memory_pool {

  // STL 分配器适配器：统一经全局内存池（DefaultMemoryPool()）分配，跨容器共享同一池。
  //
  // 记账由 MemoryPool 的「自描述头部」承担（见 block.hpp），因此：
  //   - allocate 走池的带头部路径，保证满足 alignof(T)（含超对齐 T，如 alignas(32)）；
  //   - deallocate 无需依赖传入的 n，尺寸以头部记录为准。
  template<typename T, typename Config = DefaultConfig>
  class Allocator {
  public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;

    // 构造函数
    constexpr Allocator() noexcept = default;

    // 拷贝构造函数
    template<typename U>
    constexpr Allocator(const Allocator<U, Config> &) noexcept {}

    // 分配 n 个 T
    [[nodiscard]] T *allocate(size_type n) {
      if (n > std::numeric_limits<size_type>::max() / sizeof(T)) {
        throw std::bad_alloc();
      }
      // 经全局池的带头部分配原语，保证 alignof(T) 对齐（含超对齐 T）
      return static_cast<T *>(detail::AllocateWithHeader(n * sizeof(T), alignof(T)));
    }

    // 释放（尺寸以头部记录为准，忽略 n）
    void deallocate(T *p, size_type /*n*/) noexcept { detail::FreeWithHeader(p); }
  };

  // 分配器相等性比较
  template<typename T, typename U, typename Config>
  constexpr bool operator==(const Allocator<T, Config> &, const Allocator<U, Config> &) noexcept {
    return true;
  }

  // 分配器不等性比较
  template<typename T, typename U, typename Config>
  constexpr bool operator!=(const Allocator<T, Config> &, const Allocator<U, Config> &) noexcept {
    return false;
  }

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_ALLOCATOR_HPP_
