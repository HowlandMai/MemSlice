// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_POOL_HPP_
#define MEMORY_POOL_MEMORY_POOL_POOL_HPP_

#include <cstddef>
#include <cstring>
#include <new>

#include "config.hpp"
#include "first_level.hpp"
#include "second_level.hpp"

namespace memory_pool {

  // 内存池主类（根据内存大小自动选择一级或二级分配器）
  template<typename Config = DefaultConfig>
  class MemoryPool {
  private:
    FirstLevelAllocator<Config> first_level_alloc_;
    SecondLevelAllocator<Config> second_level_alloc_;

  public:
    // 构造函数
    MemoryPool() = default;

    // 禁止拷贝构造和赋值操作
    MemoryPool(const MemoryPool &) = delete;
    MemoryPool &operator=(const MemoryPool &) = delete;

    // 分配内存
    [[nodiscard]] void *Allocate(size_t n) {
      if (n > Config::kMaxSmallObjectBytes) {
        void *result = first_level_alloc_.Allocate(n);
        if (result == nullptr) {
          throw std::bad_alloc();
        }
        return result;
      } else {
        return second_level_alloc_.Allocate(n);
      }
    }

    // 释放内存
    void Deallocate(void *p, size_t n) noexcept {
      if (p == nullptr) {
        return;
      }

      if (n > Config::kMaxSmallObjectBytes) {
        first_level_alloc_.Deallocate(p, n);
      } else {
        second_level_alloc_.Deallocate(p, n);
      }
    }

    // 重新分配内存
    [[nodiscard]] void *Reallocate(void *p, size_t old_size, size_t new_size) {
      if (p == nullptr) {
        return Allocate(new_size);
      }

      if (old_size > Config::kMaxSmallObjectBytes && new_size > Config::kMaxSmallObjectBytes) {
        void *result = first_level_alloc_.Reallocate(p, old_size, new_size);
        if (result == nullptr) {
          throw std::bad_alloc();
        }
        return result;
      } else if (new_size <= Config::kMaxSmallObjectBytes) {
        void *new_p = Allocate(new_size);
        size_t copy_size = (old_size < new_size) ? old_size : new_size;
        std::memcpy(new_p, p, copy_size);
        Deallocate(p, old_size);
        return new_p;
      } else {
        void *new_p = Allocate(new_size);
        std::memcpy(new_p, p, old_size);
        Deallocate(p, old_size);
        return new_p;
      }
    }

    // 当前池向系统申请的总字节数（用于诊断/测试）
    [[nodiscard]] size_t heap_size() const noexcept { return second_level_alloc_.heap_size(); }
  };

  // 全局内存池（泄漏式单例，见 src/memory_pool.cpp 实现）。
  // 采用函数内 static + placement new 泄漏：永不析构，消除退出期析构顺序问题与静态构造顺序问题。
  MemoryPool<> &DefaultMemoryPool();

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_POOL_HPP_
