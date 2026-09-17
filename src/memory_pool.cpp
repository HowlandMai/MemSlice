// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#include "../include/memory_pool.hpp"

namespace memory_pool {

  // 全局内存池：函数内 static + placement new 的泄漏式单例。
  // 池本体建于系统堆上（placement new 不走被重载的全局 operator new，避免递归初始化），
  // 刻意永不析构（泄漏）以彻底消除两类全局生命周期问题：
  //  1. 构造顺序：其它 TU 的全局对象构造中执行 new 时，池一定已就绪（首次调用即构造）；
  //  2. 析构顺序：池永不析构，其它全局对象析构中执行 delete 不会撞上已释放的池。
  MemoryPool<> &DefaultMemoryPool() {
    static MemoryPool<> *pool = [] {
      void *storage = std::malloc(sizeof(MemoryPool<>));
      if (storage == nullptr) {
        throw std::bad_alloc();
      }
      return new (storage) MemoryPool<>();
    }();
    return *pool;
  }

} // namespace memory_pool

// 重载全局 new 以使用内存池。所有分配都在用户指针前放置头部（详见头文件 detail），
// 记录真实底层基址与总字节数，无参 delete 也能正确回收。
void *operator new(std::size_t size) {
  return memory_pool::detail::AllocateWithHeader(size, alignof(std::max_align_t));
}

// 超对齐版本（C++17 及以上）：对齐需求由分配原语统一满足，按请求对齐返回
void *operator new(std::size_t size, std::align_val_t alignment) {
  return memory_pool::detail::AllocateWithHeader(size, static_cast<std::size_t>(alignment));
}

// 无参 delete：从头部恢复基址与尺寸
void operator delete(void *p) noexcept { memory_pool::detail::FreeWithHeader(p); }

// 带尺寸版本：以头部为准，保证与分配保持一致
void operator delete(void *p, std::size_t /*size*/) noexcept { memory_pool::detail::FreeWithHeader(p); }

void operator delete(void *p, std::align_val_t /*alignment*/) noexcept { memory_pool::detail::FreeWithHeader(p); }

void operator delete(void *p, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
  memory_pool::detail::FreeWithHeader(p);
}

void *operator new[](std::size_t size) {
  return memory_pool::detail::AllocateWithHeader(size, alignof(std::max_align_t));
}

void *operator new[](std::size_t size, std::align_val_t alignment) {
  return memory_pool::detail::AllocateWithHeader(size, static_cast<std::size_t>(alignment));
}

void operator delete[](void *p) noexcept { memory_pool::detail::FreeWithHeader(p); }

void operator delete[](void *p, std::size_t /*size*/) noexcept { memory_pool::detail::FreeWithHeader(p); }

void operator delete[](void *p, std::align_val_t /*alignment*/) noexcept {
  memory_pool::detail::FreeWithHeader(p);
}

void operator delete[](void *p, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
  memory_pool::detail::FreeWithHeader(p);
}