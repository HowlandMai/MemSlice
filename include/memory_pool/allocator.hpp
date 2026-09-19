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

  // 带头部与对齐的分配原语：供全局 new/delete 重载与 Allocator<T> 共用。
  // 头部位于用户指针正前方，记录真实基址与总字节数，使无参 delete 也能正确回收。
  namespace detail {
    // 用户指针前的头部结构
    struct RawHeader {
      void *base;
      size_t size;
    };

    // 头部字节数（不小于 RawHeader 实际大小，且为对齐数的整数倍）
    constexpr std::size_t kHeaderSize = 16;

    [[nodiscard]] constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
      return (value + alignment - 1) & ~(alignment - 1);
    }

    [[nodiscard]] constexpr std::size_t MaxAlign() noexcept { return alignof(std::max_align_t); }

    [[nodiscard]] inline RawHeader *HeaderOf(void *p) noexcept {
      return reinterpret_cast<RawHeader *>(static_cast<char *>(p) - kHeaderSize);
    }

    // 分配 size 字节并保证 alignment 对齐。头部紧邻用户指针前（p - kHeaderSize 处）。
    // 对 alignment ≤ max_align 的请求同样按 max_align 对齐：池按 8 字节分桶只保证
    // 8 字节对齐，而普通 new 的契约是 max_align_t——必须显式对齐到该粒度。
    [[nodiscard]] inline void *AllocateWithHeader(std::size_t size, std::size_t alignment) {
      const std::size_t align = (alignment < MaxAlign()) ? MaxAlign() : alignment;
      const std::size_t total = AlignUp(kHeaderSize + size, align) + align;
      if (total < size) {
        throw std::bad_alloc(); // kHeaderSize + size 回绕溢出
      }
      void *raw = DefaultMemoryPool().Allocate(total);
      std::uintptr_t p = AlignUp(reinterpret_cast<std::uintptr_t>(raw) + kHeaderSize, align);
      RawHeader *h = reinterpret_cast<RawHeader *>(p - kHeaderSize);
      h->base = raw;
      h->size = total;
      return reinterpret_cast<void *>(p);
    }

    // 释放 AllocateWithHeader 返回的指针（以头部记录为准）
    inline void FreeWithHeader(void *p) noexcept {
      if (p == nullptr) {
        return;
      }
      RawHeader *h = HeaderOf(p);
      DefaultMemoryPool().Deallocate(h->base, h->size);
    }
  } // namespace detail

  // 内存分配器类型（符合 STL 分配器要求）
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

    // 分配内存
    [[nodiscard]] T *allocate(size_type n) {
      if (n > std::numeric_limits<size_type>::max() / sizeof(T)) {
        throw std::bad_alloc();
      }
      // 走带头部路径以保证 alignof(T) 对齐（含超对齐 T）；deallocate 以头部记录为准
      return static_cast<T *>(detail::AllocateWithHeader(n * sizeof(T), alignof(T)));
    }

    // 释放内存
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
