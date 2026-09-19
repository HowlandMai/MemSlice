// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_SECOND_LEVEL_HPP_
#define MEMORY_POOL_MEMORY_POOL_SECOND_LEVEL_HPP_

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <type_traits>

#include "block.hpp"
#include "config.hpp"

namespace memory_pool {

  // 二级分配器（管理小内存块）
  template<typename Config>
  class SecondLevelAllocator {
  private:
    // 配置校验：对齐大小必须为正、为 2 的幂，且最大小对象字节数必须为其整数倍
    static_assert(Config::kAlignSize > 0, "对齐大小必须为正数");
    static_assert((Config::kAlignSize & (Config::kAlignSize - 1)) == 0, "对齐大小必须是 2 的幂");
    static_assert(Config::kMaxSmallObjectBytes > 0, "最大小对象字节数必须为正数");
    static_assert(Config::kMaxSmallObjectBytes % Config::kAlignSize == 0, "最大小对象字节数必须是对齐大小的整数倍");
    static_assert(Config::kDefaultNobjs > 0, "默认申请对象数必须为正数");

    // 计算空闲链表数量
    static constexpr size_t kNumFreeLists = Config::kMaxSmallObjectBytes / Config::kAlignSize;

    // 获取内存对齐后的大小（对 0 字节按最小粒度处理，避免无符号下溢）
    [[nodiscard]] static constexpr size_t RoundUp(size_t bytes) noexcept {
      if (bytes == 0) {
        return Config::kAlignSize; // 0 字节分配按最小粒度处理，避免 (0-1) 下溢
      }
      return (bytes + Config::kAlignSize - 1) & ~(Config::kAlignSize - 1);
    }

    // 获取对应的空闲链表索引
    [[nodiscard]] static constexpr size_t FreeListIndex(size_t bytes) noexcept {
      return (RoundUp(bytes) - 1) / Config::kAlignSize;
    }

    // 空闲链表数组
    MemBlock *free_list_[kNumFreeLists]{};

    // 指向当前内存块的起始位置
    char *start_free_ = nullptr;

    // 指向当前内存块的结束位置
    char *end_free_ = nullptr;

    // 内存池已分配的总内存大小
    size_t heap_size_ = 0;

    // 已申请内存块的基址链表（用于析构时回收）
    struct ChunkNode {
      char *base;
      size_t size;
      ChunkNode *next;
    };
    ChunkNode *chunk_list_ = nullptr;

    // 线程安全开关：为真时使用互斥锁，为假时使用空操作锁
    struct NoopLock {
      void lock() noexcept {}
      void unlock() noexcept {}
    };
    using MutexT = std::conditional_t<detail::kThreadSafeConfig<Config>, std::mutex, NoopLock>;
    using LockGuard = std::lock_guard<MutexT>;
    mutable MutexT mutex_;

  public:
    // 构造函数
    SecondLevelAllocator() = default;

    // 析构函数：回收所有向系统申请的内存块，避免内存泄漏
    ~SecondLevelAllocator() {
      ChunkNode *n = chunk_list_;
      while (n != nullptr) {
        std::free(n->base);
        ChunkNode *cur = n;
        n = n->next;
        std::free(cur);
      }
      chunk_list_ = nullptr;
    }

    // 禁止拷贝构造和赋值操作
    SecondLevelAllocator(const SecondLevelAllocator &) = delete;
    SecondLevelAllocator &operator=(const SecondLevelAllocator &) = delete;

    // 单次分配可用的最小/最大底层块容量（供上层判断某请求是否可由本分配器承接）
    static constexpr size_t kMinBlockBytes = Config::kAlignSize;
    static constexpr size_t kMaxBlockBytes = Config::kMaxSmallObjectBytes;

    // 分配内存
    [[nodiscard]] void *Allocate(size_t n) {
      LockGuard guard(mutex_);
      return AllocateImpl(n);
    }

    // 释放内存
    void Deallocate(void *p, size_t n) noexcept {
      LockGuard guard(mutex_);
      DeallocateImpl(p, n);
    }

    // 重新分配内存
    [[nodiscard]] void *Reallocate(void *p, size_t old_size, size_t new_size) {
      LockGuard guard(mutex_);
      return ReallocateImpl(p, old_size, new_size);
    }

    // 当前池向系统申请的总字节数（线程安全，内部加锁）
    [[nodiscard]] size_t heap_size() const noexcept {
      LockGuard guard(mutex_);
      return heap_size_;
    }

    // ---- Raw 接口：调用方（MemoryPool 头部路径）以真实块容量为准，语义等价于上组 ----

    // 该块容量是否可由本分配器承担
    [[nodiscard]] static constexpr bool CanServe(size_t block_bytes) noexcept {
      return block_bytes >= kMinBlockBytes && block_bytes <= kMaxBlockBytes;
    }

    // 按块容量分配（容量须落在 [kMinBlockBytes, kMaxBlockBytes] 内）
    [[nodiscard]] void *RawAllocate(size_t block_bytes) {
      LockGuard guard(mutex_);
      return AllocateImpl(block_bytes);
    }

    // 按块容量回收
    void RawDeallocate(void *p, size_t block_bytes) noexcept {
      LockGuard guard(mutex_);
      DeallocateImpl(p, block_bytes);
    }

  private:
    // 分配内存（调用方已持有锁）
    [[nodiscard]] void *AllocateImpl(size_t n) {
      size_t aligned_size = RoundUp(n);
      // 统一在调试与发布构建下拒绝越界请求：避免 free_list_ 越界读写。
      // 越界请求按分配失败处理，而非未定义行为
      if (aligned_size > Config::kMaxSmallObjectBytes) {
        throw std::bad_alloc();
      }

      MemBlock **my_free_list = free_list_ + FreeListIndex(aligned_size);
      MemBlock *result = *my_free_list;

      if (result == nullptr) {
        return Refill(aligned_size);
      }

      *my_free_list = result->next;
      return result;
    }

    // 释放内存（调用方已持有锁）
    void DeallocateImpl(void *p, size_t n) noexcept {
      if (p == nullptr) {
        return;
      }

      size_t aligned_size = RoundUp(n);
      assert(aligned_size <= Config::kMaxSmallObjectBytes);
      if (aligned_size > Config::kMaxSmallObjectBytes) {
        // 调试构建已断言；发布构建下丢弃错误请求，避免越界
        return;
      }

      MemBlock **my_free_list = free_list_ + FreeListIndex(aligned_size);
      MemBlock *q = reinterpret_cast<MemBlock *>(p);
      q->next = *my_free_list;
      *my_free_list = q;
    }

    // 重新分配内存（调用方已持有锁）
    [[nodiscard]] void *ReallocateImpl(void *p, size_t old_size, size_t new_size) {
      if (p == nullptr) {
        return AllocateImpl(new_size);
      }
      void *result = AllocateImpl(new_size);
      size_t copy_size = (old_size < new_size) ? old_size : new_size;
      std::memcpy(result, p, copy_size);
      DeallocateImpl(p, old_size);
      return result;
    }

    // 向系统申请内存并分配给空闲链表
    [[nodiscard]] void *Refill(size_t n) {
      int nobjs = Config::kDefaultNobjs;

      char *chunk = ChunkAlloc(n, nobjs);
      assert(chunk != nullptr);

      if (nobjs == 1) {
        return chunk;
      }

      MemBlock **my_free_list = free_list_ + FreeListIndex(n);
      MemBlock *result = reinterpret_cast<MemBlock *>(chunk);
      *my_free_list = reinterpret_cast<MemBlock *>(chunk + n);
      MemBlock *current = *my_free_list;

      for (int i = 1;; ++i) {
        MemBlock *next = reinterpret_cast<MemBlock *>(reinterpret_cast<char *>(current) + n);
        if (i == nobjs - 1) {
          current->next = nullptr;
          break;
        }
        current->next = next;
        current = next;
      }

      return result;
    }

    // 向系统申请大块内存
    [[nodiscard]] char *ChunkAlloc(size_t size, int &nobjs) {
      char *result;
      size_t total_bytes = size * nobjs;
      // 起始指针为空时，剩余空间视为 0（避免对空指针做减法）
      size_t bytes_left = (start_free_ == nullptr) ? 0 : (end_free_ - start_free_);

      if (bytes_left >= total_bytes) {
        result = start_free_;
        start_free_ += total_bytes;
        return result;
      } else if (bytes_left >= size) {
        nobjs = static_cast<int>(bytes_left / size);
        total_bytes = size * nobjs;
        result = start_free_;
        start_free_ += total_bytes;
        return result;
      } else {
        size_t bytes_to_get = 2 * total_bytes + RoundUp(heap_size_ >> 4);

        if (bytes_left > 0) {
          MemBlock **my_free_list = free_list_ + FreeListIndex(bytes_left);
          reinterpret_cast<MemBlock *>(start_free_)->next = *my_free_list;
          *my_free_list = reinterpret_cast<MemBlock *>(start_free_);
        }

        char *new_chunk = reinterpret_cast<char *>(std::malloc(bytes_to_get));
        if (new_chunk == nullptr) {
          for (size_t i = size; i <= Config::kMaxSmallObjectBytes; i += Config::kAlignSize) {
            MemBlock **my_free_list = free_list_ + FreeListIndex(i);
            MemBlock *p = *my_free_list;
            if (p != nullptr) {
              *my_free_list = p->next;
              start_free_ = reinterpret_cast<char *>(p);
              end_free_ = start_free_ + i;
              return ChunkAlloc(size, nobjs);
            }
          }
          throw std::bad_alloc();
        }

        // 记录新申请的内存块，供析构时回收
        auto *node = static_cast<ChunkNode *>(std::malloc(sizeof(ChunkNode)));
        if (node == nullptr) {
          std::free(new_chunk);
          throw std::bad_alloc();
        }
        node->base = new_chunk;
        node->size = bytes_to_get;
        node->next = chunk_list_;
        chunk_list_ = node;

        start_free_ = new_chunk;
        heap_size_ += bytes_to_get;
        end_free_ = start_free_ + bytes_to_get;
        return ChunkAlloc(size, nobjs);
      }
    }
  };

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_SECOND_LEVEL_HPP_
