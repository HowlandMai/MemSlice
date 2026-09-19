// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_BLOCK_HPP_
#define MEMORY_POOL_MEMORY_POOL_BLOCK_HPP_

#include <cstddef>
#include <cstdint>

#include "config.hpp"

namespace memory_pool {

  // 空闲链表节点。块空闲时该位置存放后继指针，分配出去后即成为用户数据的一部分。
  union alignas(std::max_align_t) MemBlock {
    MemBlock *next;
  };

  namespace detail {
    // 内存来源：决定回收时回到哪个分配器。
    // 取值需为 2 的幂，以便折进容量字段的低位（见 kCapacityBits）。
    enum class BlockSource : std::uintptr_t {
      kSecondLevel = 0, // 来自二级分配器（按桶复用）
      kFirstLevel = 1, // 来自一级分配器（直接 free 给系统）
    };

    // 头部内的「容量 + 来源 + 调试标记」被打包进同一个机器字：
    //   位 0-1：来源（池内块均为 8 字节对齐，故低位天然为 0）
    //   位 2  ：分配状态（1=当前已分配出去，0=已释放/在空闲链表上）
    //   位 3-8：魔数校验位（用于识别头部是否被越界写坏）
    //   位 9+ ：块容量
    // 这样头部仍只需两个机器字（16 字节），不因调试能力而增加每次分配的内存开销。
    constexpr unsigned kSourceBits = 2;
    constexpr unsigned kStateBits = 1;
    constexpr unsigned kMagicBits = 6;
    constexpr unsigned kFlagBits = kSourceBits + kStateBits + kMagicBits;

    constexpr std::uintptr_t kSourceMask = (static_cast<std::uintptr_t>(1) << kSourceBits) - 1;
    constexpr std::uintptr_t kStateMask = (static_cast<std::uintptr_t>(1) << kStateBits) - 1;
    constexpr std::uintptr_t kMagicMask = (static_cast<std::uintptr_t>(1) << kMagicBits) - 1;

    // 魔数取值：取一个不至于与常见用户数据巧合相符的位型
    constexpr std::uintptr_t kMagicValue = 0x2B; // 6 位

    constexpr std::uintptr_t kCapacityMask = ~((static_cast<std::uintptr_t>(1) << kFlagBits) - 1);
    constexpr std::uintptr_t kMaxCapacity = kCapacityMask;

    [[nodiscard]] constexpr std::uintptr_t PackFlags(std::size_t capacity, BlockSource source, bool allocated,
                                                     std::uintptr_t magic) noexcept {
      // 容量必须左移到标志位之上；低位留给来源/状态/魔数。
      return (static_cast<std::uintptr_t>(capacity) << kFlagBits) | static_cast<std::uintptr_t>(source) |
             ((allocated ? 1u : 0u) << kSourceBits) | ((magic & kMagicMask) << (kSourceBits + kStateBits));
    }

    [[nodiscard]] constexpr std::size_t UnpackCapacity(std::uintptr_t packed) noexcept {
      // 与 PackFlags 对称：容量存放在标志位之上，需右移还原。
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

    // 用户指针前的自描述头部：记录本次分配的真实基址与「容量 + 来源 + 状态 + 魔数」打包字。
    // 有了它，释放与重新分配都无需调用方再提供尺寸。
    struct RawHeader {
      void *base; // 本次分配的真实基址（回收时交还的指针）
      std::uintptr_t packed; // 容量 | 来源 | 分配状态 | 魔数
    };

    // 头部字节数由结构体自身推导（而非手写魔数），并向上取整到
    // max_align_t 的整数倍，保证用户指针前方始终有完整、对齐的头部。
    constexpr std::size_t kHeaderSize = [] {
      constexpr std::size_t raw = sizeof(RawHeader);
      constexpr std::size_t align = alignof(std::max_align_t);
      return ((raw + align - 1) / align) * align;
    }();

    static_assert(sizeof(RawHeader) <= kHeaderSize, "头部预留字节数必须不小于 RawHeader 实际大小");

    [[nodiscard]] constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
      return (value + alignment - 1) & ~(alignment - 1);
    }

    [[nodiscard]] constexpr std::size_t MaxAlign() noexcept { return alignof(std::max_align_t); }

    [[nodiscard]] inline RawHeader *HeaderOf(void *p) noexcept {
      return reinterpret_cast<RawHeader *>(static_cast<char *>(p) - kHeaderSize);
    }

    // 头部记录的块容量（承载用户数据的底层块总字节数）
    [[nodiscard]] inline std::size_t CapacityOf(const void *p) noexcept {
      return UnpackCapacity(HeaderOf(const_cast<void *>(p))->packed);
    }

    [[nodiscard]] inline BlockSource SourceOf(const void *p) noexcept {
      return UnpackSource(HeaderOf(const_cast<void *>(p))->packed);
    }

    // ---- 调试期校验（仅在调试构建启用；发布构建下这些函数为空操作）----

    // 判定头部是否「看起来像」本池写下的：魔数位必须匹配。
    // 说明：这是启发式判断，无法保证 100% 区分「用户数据恰好撞上魔数」与真实头部，
    // 但能把绝大多数野指针/未初始化指针/已被覆写的头部识别出来。
    [[nodiscard]] inline bool HeaderLooksValid(const void *p) noexcept {
      return UnpackMagic(HeaderOf(const_cast<void *>(p))->packed) == kMagicValue;
    }

    [[nodiscard]] inline bool IsAllocated(const void *p) noexcept {
      return UnpackAllocated(HeaderOf(const_cast<void *>(p))->packed);
    }

  } // namespace detail

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_BLOCK_HPP_
