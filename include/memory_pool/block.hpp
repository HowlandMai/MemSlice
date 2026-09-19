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

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_BLOCK_HPP_
