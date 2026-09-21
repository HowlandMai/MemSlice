// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

#ifndef MEMPOOL_LAB_BLOCK_HPP_
#define MEMPOOL_LAB_BLOCK_HPP_

#include <cstddef>
#include <cstdint>

#include "config.hpp"

namespace mempool_lab {

  // 空闲链表节点。
  //
  // 关键设计：空闲块的前 8 字节存放 next 指针，**不额外分配节点**。
  // 这节省了内存，但带来一条必须牢记的约束：
  //
  //   ★ next 与头部（见 detail::Header）占用同一段内存。
  //     因此一个块不可能同时处于「空闲链表态」和「已分配态」。
  //     把块挂回链表之后再去读写头部，读到的就是链表指针。
  //
  // 详见 docs/03-building-log.md 中这个约束导致过的真实缺陷。
  struct FreeNode {
    FreeNode *next;
  };

  namespace detail {

    // 块的来源：决定释放时走哪条回收路径
    enum class BlockSource : std::uintptr_t {
      kPool = 0, // 二级分配器（空闲链表）的块
      kSystem = 1, // 一级分配器（直接 malloc）的块
    };

    // 用户指针前的自描述头部。
    //
    // 布局（两个机器字）：
    //   base   : 本次分配的真实基址（回收时交还的指针）
    //   packed : 「容量 | 来源 | 状态 | 魔数」打包进一个机器字
    //
    // 为什么需要它：free(p) 只有指针、没有尺寸。有了头部，释放无需调用方提供
    // 尺寸，且能区分块的来源。这是本实现与原版 SGI 分配器的主要差异之一。
    struct Header {
      void *base;
      std::uintptr_t packed;
    };

    // ---- packed 字的位域划分 ----
    // 布局（从低位到高位）：[来源 1 位][状态 1 位][魔数 6 位][容量 ...]
    constexpr unsigned kSourceBits = 1;
    constexpr unsigned kStateBits = 1;
    constexpr unsigned kMagicBits = 6;
    constexpr unsigned kFlagBits = kSourceBits + kStateBits + kMagicBits; // 8

    constexpr std::uintptr_t kSourceMask = (static_cast<std::uintptr_t>(1) << kSourceBits) - 1;
    constexpr std::uintptr_t kStateMask = (static_cast<std::uintptr_t>(1) << kStateBits) - 1;
    constexpr std::uintptr_t kMagicMask = (static_cast<std::uintptr_t>(1) << kMagicBits) - 1;

    constexpr std::uintptr_t kMagicValue = 0x2B; // 6 位常量，用于识别"这确实是本池的头部"

    // 容量上限：能放进 packed 高位域的最大值
    constexpr std::size_t kMaxCapacity = (~static_cast<std::uintptr_t>(0)) >> kFlagBits;

    [[nodiscard]] constexpr std::uintptr_t PackFlags(std::size_t capacity, BlockSource source, bool allocated,
                                                     std::uintptr_t magic) noexcept {
      return (capacity << kFlagBits) | (static_cast<std::uintptr_t>(source) & kSourceMask) |
             ((allocated ? 1u : 0u) << kSourceBits) | ((magic & kMagicMask) << (kSourceBits + kStateBits));
    }

    [[nodiscard]] constexpr std::size_t UnpackCapacity(std::uintptr_t packed) noexcept {
      return static_cast<std::size_t>(packed >> kFlagBits);
    }

    [[nodiscard]] constexpr BlockSource UnpackSource(std::uintptr_t packed) noexcept {
      return static_cast<BlockSource>(packed & kSourceMask);
    }

    [[nodiscard]] constexpr bool UnpackAllocated(std::uintptr_t packed) noexcept {
      return ((packed >> kSourceBits) & kStateMask) != 0;
    }

    [[nodiscard]] constexpr std::uintptr_t UnpackMagic(std::uintptr_t packed) noexcept {
      return (packed >> (kSourceBits + kStateBits)) & kMagicMask;
    }

    [[nodiscard]] constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
      return (value + alignment - 1) & ~(alignment - 1);
    }

    [[nodiscard]] constexpr std::size_t MaxAlign() noexcept { return alignof(std::max_align_t); }

    // 头部占用的字节数，由结构体自身推导。
    //
    // ★ 必须向上取整到 max_align_t 的整数倍，这不是保守偏好而是硬约束：
    //   头部写在 p - kHeaderBytes 处，而用户指针
    //   p = AlignUp(raw + kHeaderBytes, align)（align ≥ max_align_t）。
    //   只有 kHeaderBytes 是 align 的整数倍时，「头部相对 base 的偏移」才恒定，
    //   分配时写入与回收时读取才算同一个地址。
    //   若取值不是 align 的整数倍，两个方向会错位——曾实测导致段错误，
    //   崩溃地址解出来是用户字符串内容（见 docs/03-building-log.md）。
    constexpr std::size_t kHeaderBytes = [] {
      constexpr std::size_t raw = sizeof(Header);
      constexpr std::size_t align = alignof(std::max_align_t);
      return ((raw + align - 1) / align) * align;
    }();

    static_assert(sizeof(Header) <= kHeaderBytes, "头部预留字节数不得小于 Header 实际大小");
    static_assert(kHeaderBytes % alignof(std::max_align_t) == 0,
                  "头部字节数必须是 max_align_t 的整数倍（否则头部与用户指针会错位）");

    // 由用户指针取回头部
    [[nodiscard]] inline Header *HeaderOf(void *p) noexcept {
      return reinterpret_cast<Header *>(static_cast<char *>(p) - kHeaderBytes);
    }

    [[nodiscard]] inline const Header *HeaderOf(const void *p) noexcept { return HeaderOf(const_cast<void *>(p)); }

    [[nodiscard]] inline std::size_t CapacityOf(const void *p) noexcept { return UnpackCapacity(HeaderOf(p)->packed); }

    [[nodiscard]] inline BlockSource SourceOf(const void *p) noexcept { return UnpackSource(HeaderOf(p)->packed); }

    // 块是否「看起来像」本池写下的：魔数位必须匹配。
    // 这是启发式判断，无法 100% 区分"用户数据恰好撞上魔数"，但能识别绝大多数
    // 野指针、未初始化指针与已被覆写的头部。
    [[nodiscard]] inline bool HeaderLooksValid(const void *p) noexcept {
      return UnpackMagic(HeaderOf(p)->packed) == kMagicValue;
    }

  } // namespace detail

} // namespace mempool_lab

#endif // MEMPOOL_LAB_BLOCK_HPP_
