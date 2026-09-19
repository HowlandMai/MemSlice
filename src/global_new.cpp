// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#include "memory_pool/global_new.hpp"
#include "memory_pool/allocator.hpp"

#include <cstddef>
#include <new>

// 重载全局 new 以使用内存池。所有分配都在用户指针前放置自描述头部
// （记录真实基址 / 底层块容量 / 请求尺寸 / 来源），因此所有 delete 形态
// 都只需指针即可正确回收，尺寸参数一律忽略。
void *operator new(std::size_t size) {
  return memory_pool::detail::AllocateWithHeader(size, alignof(std::max_align_t));
}

// 超对齐版本（C++17 及以上）：对齐需求由分配原语统一满足，按请求对齐返回
void *operator new(std::size_t size, std::align_val_t alignment) {
  return memory_pool::detail::AllocateWithHeader(size, static_cast<std::size_t>(alignment));
}

// 无参 delete：完全以头部记录为准
void operator delete(void *p) noexcept { memory_pool::detail::FreeWithHeader(p); }

// 带尺寸版本：尺寸不可信（也可能是 sizeof(void*) 等占位值），以头部为准
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

void operator delete[](void *p, std::align_val_t /*alignment*/) noexcept { memory_pool::detail::FreeWithHeader(p); }

void operator delete[](void *p, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
  memory_pool::detail::FreeWithHeader(p);
}
