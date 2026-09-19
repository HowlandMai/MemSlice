// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_FIRST_LEVEL_HPP_
#define MEMORY_POOL_MEMORY_POOL_FIRST_LEVEL_HPP_

#include <cstdlib>

namespace memory_pool {

  // 一级分配器（直接向系统申请内存）
  template<typename Config>
  class FirstLevelAllocator {
  public:
    // 分配内存
    [[nodiscard]] static void *Allocate(size_t n) noexcept { return std::malloc(n); }

    // 释放内存
    static void Deallocate(void *p, size_t /*n*/) noexcept { std::free(p); }

    // 重新分配内存
    [[nodiscard]] static void *Reallocate(void *p, size_t /*old_size*/, size_t new_size) noexcept {
      return std::realloc(p, new_size);
    }
  };

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_FIRST_LEVEL_HPP_
