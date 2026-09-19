// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_POOL_HPP_
#define MEMORY_POOL_MEMORY_POOL_POOL_HPP_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "block.hpp"
#include "config.hpp"
#include "first_level.hpp"
#include "second_level.hpp"

namespace memory_pool {

  // 内存池主类：按请求大小在两级分配器之间路由，并统一使用「自描述头部」记账。
  //
  // 记账约定（统一头部，见 block.hpp 的 detail::RawHeader）：
  //   - 每一次分配都在用户指针正前方放置头部，记录真实基址 / 底层块容量 / 请求尺寸 / 来源；
  //   - 因此 Deallocate 只需指针、无需尺寸，Reallocate 只需新尺寸——
  //     调用方再也不需要保证「释放时传入与分配一致的 n」。
  template<typename Config = DefaultConfig>
  class MemoryPool {
  private:
    FirstLevelAllocator<Config> first_level_alloc_;
    SecondLevelAllocator<Config> second_level_alloc_;

    // 头部保留字节数，它同样占用底层块容量，
    // 因此对外可见的小对象上限需扣除头部开销后才是真正可用的净字节数。
    static constexpr size_t kHeaderBytes = detail::kHeaderSize;
    static constexpr size_t kBlockAlign = detail::MaxAlign();

    // 分配 size 字节用户数据并返回其用户指针（头部已就位）
    [[nodiscard]] void *AllocateWithHeader(size_t size, size_t alignment) {
      if (size > kMaxRequestedBytes) {
        throw std::bad_alloc(); // 请求尺寸大到无法在块容量中表达
      }
      const size_t align = (alignment < kBlockAlign) ? kBlockAlign : alignment;
      // 底层块需要容纳「头部 + 用户数据」，并额外留出一个对齐单位的余量：
      // 用户指针要在 raw + kHeaderBytes 之后向上取整到 align，取整最多多消耗 align-1 字节。
      const size_t total = detail::AlignUp(kHeaderBytes + size, align) + (align - kBlockAlign);
      if (total < size) {
        throw std::bad_alloc(); // kHeaderBytes + size 回绕溢出
      }

      detail::BlockSource source;
      void *raw;
      if (SecondLevelAllocator<Config>::CanServe(total)) {
        raw = second_level_alloc_.RawAllocate(total);
        source = detail::BlockSource::kSecondLevel;
      } else {
        raw = first_level_alloc_.Allocate(total);
        if (raw == nullptr) {
          throw std::bad_alloc();
        }
        source = detail::BlockSource::kFirstLevel;
      }

      // 用户指针须按 align 对齐，且头部紧邻其前方
      std::uintptr_t p = detail::AlignUp(reinterpret_cast<std::uintptr_t>(raw) + kHeaderBytes, align);
      detail::RawHeader *h = reinterpret_cast<detail::RawHeader *>(p - kHeaderBytes);
      h->base = raw;
      h->capacity = total;
      h->requested = size;
      h->source = source;
      return reinterpret_cast<void *>(p);
    }

    // 以头部记录为准回收（释放与重新分配共用）
    void FreeWithHeader(void *p) noexcept {
      detail::RawHeader *h = detail::HeaderOf(p);
      if (h->source == detail::BlockSource::kSecondLevel) {
        second_level_alloc_.RawDeallocate(h->base, h->capacity);
      } else {
        first_level_alloc_.Deallocate(h->base, h->capacity);
      }
    }

  public:
    // 理论上的最大可请求尺寸：仅用于拦截大到在块容量中无法表达（会回绕）的请求。
    // 注意这里**不是**「是否走二级分配器」的分界——超出小对象上限的请求会正常
    // 回落到一级分配器。若误把分界当上限，将会错误地拒绝合法的中等尺寸请求。
    static constexpr size_t kMaxRequestedBytes = static_cast<size_t>(-1) - kHeaderBytes - kBlockAlign;

    // 构造函数
    MemoryPool() = default;

    // 禁止拷贝构造和赋值操作
    MemoryPool(const MemoryPool &) = delete;
    MemoryPool &operator=(const MemoryPool &) = delete;

    // 分配 size 字节，返回可直接使用的用户指针
    [[nodiscard]] void *Allocate(size_t size) { return AllocateWithHeader(size, kBlockAlign); }

    // 分配 size 字节并保证 alignment 对齐（供全局 new 与 Allocator<T> 的超对齐请求使用）
    [[nodiscard]] void *AllocateAligned(size_t size, size_t alignment) { return AllocateWithHeader(size, alignment); }

    // 释放：尺寸由头部记录决定，调用方无需（也不应）再提供
    void Deallocate(void *p) noexcept {
      if (p == nullptr) {
        return;
      }
      FreeWithHeader(p);
    }

    // 重新分配：新尺寸由调用方给出，旧尺寸以头部记录为准。
    // 全程只走「分配 + 拷贝 + 回收」，绝不把池内指针交给 glibc realloc，
    // 因此不存在旧实现中「谎报 old_size 触发 realloc(池内指针) 导致堆损坏」的路径。
    [[nodiscard]] void *Reallocate(void *p, size_t new_size) {
      if (p == nullptr) {
        return Allocate(new_size);
      }

      const size_t old_size = detail::RequestedOf(p);
      const size_t copy_size = (old_size < new_size) ? old_size : new_size;

      void *new_p = Allocate(new_size);
      std::memcpy(new_p, p, copy_size);
      Deallocate(p);
      return new_p;
    }

    // 当前池向系统申请的总字节数（用于诊断/测试）
    [[nodiscard]] size_t heap_size() const noexcept { return second_level_alloc_.heap_size(); }
  };

  // 全局内存池（泄漏式单例，见 src/default_pool.cpp 实现）。
  // 采用函数内 static + placement new 泄漏：永不析构，消除退出期析构顺序问题与静态构造顺序问题。
  MemoryPool<> &DefaultMemoryPool();

  // 经全局池的分配/释放原语：供全局 new/delete 重载与 Allocator<T> 共用。
  // 头部记账与对齐保证均由 MemoryPool 内部实现，此处仅做转发。
  namespace detail {
    [[nodiscard]] inline void *AllocateWithHeader(std::size_t size, std::size_t alignment) {
      return DefaultMemoryPool().AllocateAligned(size, alignment);
    }

    inline void FreeWithHeader(void *p) noexcept { DefaultMemoryPool().Deallocate(p); }
  } // namespace detail

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_POOL_HPP_
