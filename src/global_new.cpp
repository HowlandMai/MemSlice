// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#include "memory_pool/allocator.hpp"
#include "memory_pool/global_new.hpp"

#include <cstddef>
#include <new>

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
