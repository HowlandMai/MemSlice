// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#include "../include/memory_pool.hpp"
#include <cstdint>

namespace memory_pool {

  // 全局内存池实例
  MemoryPool<> default_memory_pool;

} // namespace memory_pool

namespace {

  // 用户指针前的头部：记录真实的底层基址与总字节数，使无参 delete 也能正确回收
  struct RawHeader {
    void *base;
    size_t size;
  };

  // 头部字节数必须不小于头部结构体的实际大小（16 字节），并取最大对齐次数的整数倍，
  // 以保证「底层地址 + 头部字节数」仍处于内存池返回的对齐范围内，
  // 从而满足 new 的基本对齐契约。
  constexpr std::size_t kHeaderSize = 16;

  constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
  }

  RawHeader *HeaderOf(void *p) noexcept { return reinterpret_cast<RawHeader *>(static_cast<char *>(p) - kHeaderSize); }

  // 普通（基本对齐）分配：内存池分配「头部字节数 + 请求大小」，头部记录基址与总字节数
  void *AllocWithHeader(std::size_t size) {
    const std::size_t total = kHeaderSize + size;
    void *raw = memory_pool::default_memory_pool.Allocate(total);
    RawHeader *h = static_cast<RawHeader *>(raw);
    h->base = raw;
    h->size = total;
    return static_cast<char *>(raw) + kHeaderSize;
  }

  // 超对齐分配：超额申请并在内部对齐到指定边界，头部放在对齐指针前头部长字节数处
  void *AlignedAllocWithHeader(std::size_t size, std::size_t align) {
    const std::size_t total = AlignUp(kHeaderSize + size, align) + align;
    void *raw = memory_pool::default_memory_pool.Allocate(total);
    std::uintptr_t p = AlignUp(reinterpret_cast<std::uintptr_t>(raw) + kHeaderSize, align);
    RawHeader *h = reinterpret_cast<RawHeader *>(p - kHeaderSize);
    h->base = raw;
    h->size = total;
    return reinterpret_cast<void *>(p);
  }

  void FreeWithHeader(void *p) noexcept {
    if (p == nullptr) {
      return;
    }
    RawHeader *h = HeaderOf(p);
    memory_pool::default_memory_pool.Deallocate(h->base, h->size);
  }

} // namespace

// 重载全局 new 以使用内存池
void *operator new(std::size_t size) { return AllocWithHeader(size); }

// 超对齐版本（C++17 及以上）：对齐超过默认池的对齐粒度时走手动对齐路径，
// 以保证返回内存严格满足请求的对齐要求。
void *operator new(std::size_t size, std::align_val_t alignment) {
  if (alignment <= std::align_val_t(memory_pool::DefaultConfig::kAlignSize)) {
    return operator new(size);
  }
  return AlignedAllocWithHeader(size, static_cast<std::size_t>(alignment));
}

// 无参 delete：从头部恢复基址与尺寸
void operator delete(void *p) noexcept { FreeWithHeader(p); }

// 带尺寸版本：以头部为准，保证与分配保持一致
void operator delete(void *p, std::size_t /*size*/) noexcept { FreeWithHeader(p); }

void operator delete(void *p, std::align_val_t /*alignment*/) noexcept { FreeWithHeader(p); }

void operator delete(void *p, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept { FreeWithHeader(p); }

void *operator new[](std::size_t size) { return AllocWithHeader(size); }

void *operator new[](std::size_t size, std::align_val_t alignment) {
  if (alignment <= std::align_val_t(memory_pool::DefaultConfig::kAlignSize)) {
    return operator new[](size);
  }
  return AlignedAllocWithHeader(size, static_cast<std::size_t>(alignment));
}

void operator delete[](void *p) noexcept { FreeWithHeader(p); }

void operator delete[](void *p, std::size_t /*size*/) noexcept { FreeWithHeader(p); }

void operator delete[](void *p, std::align_val_t /*alignment*/) noexcept { FreeWithHeader(p); }

void operator delete[](void *p, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept { FreeWithHeader(p); }
