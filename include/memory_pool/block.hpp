// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_BLOCK_HPP_
#define MEMORY_POOL_MEMORY_POOL_BLOCK_HPP_

#include <cstddef>
#include <cstdint>

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

    // 头部内的容量与来源被打包进同一个机器字：
    //   低 3 位存来源（池内块均为 8 字节对齐，故低位天然为 0），高位存块容量。
    // 这样头部只需两个机器字（16 字节），而非三个（24）或四个字段（32）。
    constexpr unsigned kCapacityBits = 3;
    constexpr std::uintptr_t kCapacityMask = (static_cast<std::uintptr_t>(1) << kCapacityBits) - 1;
    constexpr std::uintptr_t kMaxCapacity = ~kCapacityMask;

    [[nodiscard]] constexpr std::uintptr_t PackCapacity(std::size_t capacity, BlockSource source) noexcept {
      return static_cast<std::uintptr_t>(capacity) | static_cast<std::uintptr_t>(source);
    }

    [[nodiscard]] constexpr std::size_t UnpackCapacity(std::uintptr_t packed) noexcept {
      return static_cast<std::size_t>(packed & kMaxCapacity);
    }

    [[nodiscard]] constexpr BlockSource UnpackSource(std::uintptr_t packed) noexcept {
      return static_cast<BlockSource>(packed & kCapacityMask);
    }

    // 用户指针前的自描述头部：记录本次分配的真实基址与「容量 + 来源」打包字。
    // 有了它，释放与重新分配都无需调用方再提供尺寸。
    struct RawHeader {
      void *base; // 本次分配的真实基址（回收时交还的指针）
      std::uintptr_t packed; // 块容量 | 来源（低 3 位）
    };

    // 头部字节数由结构体自身推导（而非手写魔数），并向上取整到
    // max_align_t 的整数倍，保证用户指针前方始终有完整、对齐的头部。
    constexpr std::size_t kHeaderSize = [] {
      constexpr std::size_t raw = sizeof(RawHeader);
      constexpr std::size_t align = alignof(std::max_align_t);
      return ((raw + align - 1) / align) * align;
    }();

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
  } // namespace detail

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_BLOCK_HPP_
