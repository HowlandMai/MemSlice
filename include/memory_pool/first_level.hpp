// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_FIRST_LEVEL_HPP_
#define MEMORY_POOL_MEMORY_POOL_FIRST_LEVEL_HPP_

#include <cstddef>
#include <cstdlib>

namespace memory_pool::detail {

  // 一级分配器（直接向系统申请内存）。
  // 行为不依赖任何配置，因此不是模板——避免每个 Config 实例化出一份相同代码。
  namespace first_level {
    // 分配内存
    [[nodiscard]] inline void *Allocate(std::size_t n) noexcept { return std::malloc(n); }

    // 释放内存
    inline void Deallocate(void *p, std::size_t /*n*/) noexcept { std::free(p); }
  } // namespace first_level

} // namespace memory_pool::detail

#endif // MEMORY_POOL_MEMORY_POOL_FIRST_LEVEL_HPP_
