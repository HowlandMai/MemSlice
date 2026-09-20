// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

#ifndef MEMPOOL_LAB_ALLOCATOR_HPP_
#define MEMPOOL_LAB_ALLOCATOR_HPP_

#include <cstddef>
#include <new>

#include "block.hpp"
#include "config.hpp"
#include "pool.hpp"

namespace mempool_lab {

  // 符合 C++20 Allocator 要求的分配器，可交给 std::vector / std::list /
  // std::map 等标准容器使用。
  //
  // ## 用法
  //
  //     std::vector<int, mempool_lab::Allocator<int>> v;
  //     std::list<int, mempool_lab::Allocator<int>> l;
  //
  // ## C++20 的要求（比 C++17 少了几项）
  //
  // C++20 起不再需要显式提供 pointer / const_pointer / size_type /
  // 拷贝构造 / operator==（部分有默认推导），核心要求是：
  //   1. `value_type`
  //   2. `allocate(n)` / `deallocate(p, n)`
  //   3. 可拷贝构造（容器会按值持有分配器）
  //   4. 同类型分配器之间必须**可比较相等**——见下
  //
  // ## 分配器的"相等"语义（容易漏掉的一点）
  //
  // 标准要求：若两个分配器相等，则一个分配的存储可由另一个释放。
  // 本实现中所有 Allocator<T, Config> 都用同一个池（见 Pool()），故恒相等。
  //
  //     template <typename U>
  //     bool operator==(const Allocator<U, Config> &) const noexcept { return true; }
  //
  // 这条不能省：容器在移动/交换分配器时会比较它们，若判定**不等**，
  // 就必须逐元素搬移而不是直接窃取缓冲区（性能与语义都会变）。
  //
  // ## 超对齐（`alignof(T) > max_align_t`）
  //
  // 池的头部紧邻用户指针前方，且必须满足「头部尺寸是请求对齐的整数倍」，
  // 否则 `deallocate` 时 `p - kHeaderBytes` 无法落回头部。
  // 因此超对齐请求**不能**在分配器这一层靠手工偏移指针解决
  // （那样释放时无法还原基址），必须由池按 align 计算头部位置。
  // 本实现调用 `MemoryPool::AllocateAligned(bytes, align)` 完成。
  template<typename T, typename Config = DefaultConfig>
  class Allocator {
  public:
    using value_type = T;

    // 供容器 rebind（例如 std::list 需要按节点类型重新绑定）
    template<typename U>
    struct rebind {
      using other = Allocator<U, Config>;
    };

    Allocator() noexcept = default;

    // 从 Allocator<U> 构造：rebind 路径。所有实例共用同一个池，故无需携带状态。
    template<typename U>
    explicit Allocator(const Allocator<U, Config> &) noexcept {}

    [[nodiscard]] T *allocate(std::size_t n) {
      if (n == 0) {
        return nullptr;
      }
      // 溢出检查：n * sizeof(T) 不得回绕
      if (n > (static_cast<std::size_t>(-1) / sizeof(T))) {
        throw std::bad_alloc();
      }
      return static_cast<T *>(Pool().AllocateAligned(n * sizeof(T), alignof(T)));
    }

    void deallocate(T *p, std::size_t /*n*/) noexcept {
      if (p == nullptr) {
        return;
      }
      // 刻意不使用参数 n：尺寸由块头部记录。
      // 即便调用方传了一个错误的 n，也不会影响正确性——
      // 这正是自描述头部相较于"要求调用方传尺寸"的接口优势。
      Pool().Deallocate(p);
    }

    // 恒相等：所有实例服务于同一个池
    template<typename U>
    [[nodiscard]] bool operator==(const Allocator<U, Config> &) const noexcept {
      return true;
    }

    template<typename U>
    [[nodiscard]] bool operator!=(const Allocator<U, Config> &) const noexcept {
      return false;
    }

  private:
    // 本分配器共用的池。刻意不析构（与 DefaultPool 同理）：避免静态对象
    // 析构顺序问题——容器可能比它晚析构。
    [[nodiscard]] static MemoryPool<Config> &Pool() noexcept {
      static MemoryPool<Config> *pool = new MemoryPool<Config>();
      return *pool;
    }
  };

} // namespace mempool_lab

#endif // MEMPOOL_LAB_ALLOCATOR_HPP_
