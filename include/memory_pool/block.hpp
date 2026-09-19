// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_BLOCK_HPP_
#define MEMORY_POOL_MEMORY_POOL_BLOCK_HPP_

#include <cstddef>
#include <cstdint>

namespace memory_pool {

  // 内存块结构
  union alignas(std::max_align_t) MemBlock {
    MemBlock *next; // 指向下一个空闲内存块
    char data[1]; // 内存块数据区域
  };

  // 统一分配头部：位于用户指针正前方（p - kHeaderSize 处），自描述本次分配的来源与容量。
  // 有了它，释放与重新分配都无需调用方再提供尺寸——彻底消除「尺寸必须成对」的调用契约。
  namespace detail {
    // 内存来源：决定回收时回到哪个分配器
    enum class BlockSource : std::uint32_t {
      kSecondLevel = 0, // 来自二级分配器（按桶复用）
      kFirstLevel = 1, // 来自一级分配器（直接 free 给系统）
    };

    // 用户指针前的头部结构
    struct RawHeader {
      void *base; // 本次分配的真实基址（回收时交还的指针）
      std::size_t capacity; // 底层块的总字节数（回收时使用的尺寸）
      std::size_t requested; // 调用方请求的字节数（供 Reallocate 判定拷贝量）
      BlockSource source; // 来源：一级 or 二级分配器
    };

    // 头部字节数：必须 >= sizeof(RawHeader)（当前 32 字节）且为对齐数的整数倍，
    // 保证头部紧邻用户指针前方时不会与用户数据重叠。
    constexpr std::size_t kHeaderSize = 32;

    static_assert(sizeof(RawHeader) <= kHeaderSize, "头部预留字节数必须不小于 RawHeader 实际大小");

    [[nodiscard]] constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
      return (value + alignment - 1) & ~(alignment - 1);
    }

    [[nodiscard]] constexpr std::size_t MaxAlign() noexcept { return alignof(std::max_align_t); }

    [[nodiscard]] inline RawHeader *HeaderOf(void *p) noexcept {
      return reinterpret_cast<RawHeader *>(static_cast<char *>(p) - kHeaderSize);
    }

    // 读取头部记录的调用方请求尺寸（用于 Reallocate 计算拷贝量）
    [[nodiscard]] inline std::size_t RequestedOf(const void *p) noexcept {
      return HeaderOf(const_cast<void *>(p))->requested;
    }
  } // namespace detail

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_BLOCK_HPP_
