// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_POOL_HPP_
#define MEMORY_POOL_MEMORY_POOL_POOL_HPP_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "block.hpp"
#include "config.hpp"
#include "first_level.hpp"
#include "second_level.hpp"

namespace memory_pool {

  // 内存池主类：按请求大小在两级分配器之间路由，并统一使用「自描述头部」记账。
  //
  // 记账约定（见 block.hpp 的 detail::RawHeader）：
  //   - 每一次分配都在用户指针正前方放置 16 字节头部，记录真实基址与「容量 | 来源」；
  //   - 因此 Deallocate 只需指针、Reallocate 只需新尺寸——调用方无需保证尺寸成对。
  template<typename Config = DefaultConfig>
  class MemoryPool {
  private:
    SecondLevelAllocator<Config> second_level_alloc_;

    // 头部保留字节数（由 RawHeader 推导），它同样占用底层块容量，
    // 因此对外可见的净可用字节数需扣除头部开销与对齐余量。
    static constexpr size_t kHeaderBytes = detail::kHeaderSize;
    static constexpr size_t kBlockAlign = detail::MaxAlign();

    // 该请求是否可由二级分配器承担（按底层块容量判断，而非用户请求尺寸）
    [[nodiscard]] static constexpr bool FitsSecondLevel(size_t block_bytes) noexcept {
      return SecondLevelAllocator<Config>::CanServe(block_bytes);
    }

    // 分配 size 字节用户数据并返回其用户指针（头部已就位）
    [[nodiscard]] void *AllocateWithHeader(size_t size, size_t alignment) {
      if (size > kMaxRequestedBytes) {
        throw std::bad_alloc(); // 请求尺寸大到无法在块容量中表达
      }
      const size_t align = (alignment < kBlockAlign) ? kBlockAlign : alignment;
      // 底层块需要容纳「头部 + 用户数据」，并额外留出对齐余量：
      // 用户指针要在 raw + kHeaderBytes 之后向上取整到 align，取整最多多消耗 align - kBlockAlign 字节。
      const size_t total = detail::AlignUp(kHeaderBytes + size, align) + (align - kBlockAlign);
      if (total < size || total > detail::kMaxCapacity) {
        throw std::bad_alloc(); // 回绕溢出，或超出打包容量可表达的范围
      }

      detail::BlockSource source;
      void *raw;
      if (FitsSecondLevel(total)) {
        raw = second_level_alloc_.Allocate(total);
        source = detail::BlockSource::kSecondLevel;
      } else {
        raw = detail::first_level::Allocate(total);
        if (raw == nullptr) {
          throw std::bad_alloc();
        }
        source = detail::BlockSource::kFirstLevel;
      }

      // 用户指针须按 align 对齐，且头部紧邻其前方
      std::uintptr_t p = detail::AlignUp(reinterpret_cast<std::uintptr_t>(raw) + kHeaderBytes, align);
      detail::RawHeader *h = reinterpret_cast<detail::RawHeader *>(p - kHeaderBytes);
      h->base = raw;
      h->packed = detail::PackFlags(total, source, /*allocated=*/true, detail::kMagicValue);
      return reinterpret_cast<void *>(p);
    }

    // 以头部记录为准回收（释放与重新分配共用）
    void FreeWithHeader(void *p) noexcept {
      detail::RawHeader *h = detail::HeaderOf(p);

      // 调试期校验：把静默的堆损坏变成可定位的显式失败。
      // 发布构建下 kDebugChecks 为 false，此处不产生任何代码。
      if constexpr (detail::kDebugChecksConfig<Config>) {
        CheckBeforeFree(p, h);
      }

      const size_t capacity = detail::UnpackCapacity(h->packed);
      const detail::BlockSource source = detail::UnpackSource(h->packed);

      // 标记为已释放，使随后的重复释放可在调试构建下被识别
      h->packed = detail::PackFlags(capacity, source, /*allocated=*/false, detail::kMagicValue);

      if (source == detail::BlockSource::kSecondLevel) {
        second_level_alloc_.Deallocate(h->base, capacity);
      } else {
        detail::first_level::Deallocate(h->base, capacity);
      }
    }

    // 释放前的诊断检查（仅调试构建启用）
    static void CheckBeforeFree(void *p, detail::RawHeader *h) {
      // 1) 头部魔数：识别野指针、未初始化指针、或头部被越界写坏
      if (!detail::HeaderLooksValid(p)) {
        MemsliceFail("MemoryPool: 释放的指针头部校验失败——可能不是本池分配的指针，"
                     "或头部已被越界写坏");
      }
      // 2) 分配状态位：识别重复释放（double free）
      if (!detail::IsAllocated(p)) {
        MemsliceFail("MemoryPool: 检测到重复释放（该块已处于已释放状态）");
      }
      // 3) 基址非空（头部自身完整性）
      if (h->base == nullptr) {
        MemsliceFail("MemoryPool: 头部记录的基址为空，头部已损坏");
      }
    }

    // 调试期失败的统一处理：打印可定位信息并中止。
    // 刻意用 abort 而非异常——此时堆已处于损坏状态，继续运行只会让错误更远离现场。
    [[noreturn]] static void MemsliceFail(const char *msg) noexcept {
      std::fprintf(stderr, "[memslice] %s\n", msg); // NOLINT
      std::abort();
    }

  public:
    // 理论上的最大可请求尺寸：仅用于拦截大到在块容量中无法表达（会回绕）的请求。
    // 注意这里**不是**「是否走二级分配器」的分界——超出小对象上限的请求会正常
    // 回落到一级分配器。若误把分界当上限，将会错误地拒绝合法的中等尺寸请求。
    static constexpr size_t kMaxRequestedBytes = detail::kMaxCapacity - kHeaderBytes - kBlockAlign;

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

    // 重新分配：新尺寸由调用方给出，旧内容按头部记录的容量截取。
    // 全程只走「分配 + 拷贝 + 回收」，绝不把池内指针交给 glibc realloc，
    // 因此不存在旧实现中「谎报 old_size 触发 realloc(池内指针) 导致堆损坏」的路径。
    [[nodiscard]] void *Reallocate(void *p, size_t new_size) {
      if (p == nullptr) {
        return Allocate(new_size);
      }

      // 可安全读取的旧内容上界 = 块容量 - 头部。
      // 容量在分配时已按 AlignUp(头部 + 请求, align) 向上取整，故该值必然 ≥ 原始请求尺寸，
      // 既能完整保留旧数据，又不会越过底层块边界（不再需要单独存 requested 字段）。
      const size_t old_usable = detail::CapacityOf(p) - kHeaderBytes;
      const size_t copy_size = (old_usable < new_size) ? old_usable : new_size;

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
