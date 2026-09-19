// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_SECOND_LEVEL_HPP_
#define MEMORY_POOL_MEMORY_POOL_SECOND_LEVEL_HPP_

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <new>
#include <type_traits>

#include "block.hpp"
#include "config.hpp"

namespace memory_pool {

  // 二级分配器（管理小内存块）。
  // 公开接口以「底层块容量」为准（而非用户请求尺寸）：上层 MemoryPool 的头部记账
  // 会把头部开销与对齐余量一并算进容量，因此这里只认块容量，语义唯一、无第二套尺寸口径。
  //
  // 并发设计（分桶加锁）：每个空闲链表各持一把锁，chunk 游标（start_free_/end_free_/
  // heap_size_/chunk_list_）另持一把。不同尺寸桶之间可并发分配/回收，互不阻塞；
  // 同一尺寸桶争用同一把锁（这正是热点所在，符合预期）。
  // 加锁顺序固定为「先 chunk 锁、后桶锁」，且任何时刻不反向获取，避免死锁。
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

    // 线程安全开关：为真时使用真实互斥锁，为假时使用空操作锁（单线程零开销）
    struct NoopLock {
      void lock() noexcept {}
      void unlock() noexcept {}
    };
    using MutexT = std::conditional_t<detail::kThreadSafeConfig<Config>, std::mutex, NoopLock>;
    using LockGuard = std::lock_guard<MutexT>;

    // 空闲链表节点：数据 + 该桶自己的锁。
    // 锁类型与全局开关一致：kThreadSafe=false 时退化为 NoopLock，单线程无任何加锁开销。
    // 不同桶位于不同的结构体实例上，天然分散到不同 cache line，减少伪共享。
    struct FreeList {
      MemBlock *head = nullptr;
      mutable MutexT mutex;
    };

    // 空闲链表数组（每桶独立加锁）
    FreeList free_lists_[kNumFreeLists]{};

    // ---- 以下为 chunk 游标状态，统一由 chunk_mutex_ 保护 ----

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

    // 保护 chunk 游标状态（start_free_ / end_free_ / heap_size_ / chunk_list_）
    mutable MutexT chunk_mutex_;

  public:
    // 单次分配可用的最小/最大底层块容量（供上层判断某请求是否可由本分配器承接）
    static constexpr size_t kMinBlockBytes = Config::kAlignSize;
    static constexpr size_t kMaxBlockBytes = Config::kMaxSmallObjectBytes;

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

    // 该块容量是否可由本分配器承担
    [[nodiscard]] static constexpr bool CanServe(size_t block_bytes) noexcept {
      return block_bytes >= kMinBlockBytes && block_bytes <= kMaxBlockBytes;
    }

    // 按块容量分配（容量须落在 [kMinBlockBytes, kMaxBlockBytes] 内），失败抛 std::bad_alloc
    [[nodiscard]] void *Allocate(size_t block_bytes) {
      const size_t aligned_size = RoundUp(block_bytes);
      // 统一在调试与发布构建下拒绝越界请求：避免 free_lists_ 越界读写。
      if (aligned_size > Config::kMaxSmallObjectBytes) {
        throw std::bad_alloc();
      }

      // 快路径：只锁本桶，尝试从空闲链表摘一个块
      {
        FreeList &list = free_lists_[FreeListIndex(aligned_size)];
        LockGuard guard(list.mutex);
        MemBlock *result = list.head;
        if (result != nullptr) {
          list.head = result->next;
          return result;
        }
      }

      // 慢路径：本桶为空，需要向 chunk 要内存（会同时触及 chunk 锁与桶锁）
      return Refill(aligned_size);
    }

    // 按块容量回收（容量须与分配时一致，由上层头部保证）
    void Deallocate(void *p, size_t block_bytes) noexcept {
      if (p == nullptr) {
        return;
      }
      const size_t aligned_size = RoundUp(block_bytes);
      assert(aligned_size <= Config::kMaxSmallObjectBytes);
      if (aligned_size > Config::kMaxSmallObjectBytes) {
        // 调试构建已断言；发布构建下丢弃错误请求，避免越界
        return;
      }

      // 只锁本桶：不同尺寸的释放互不阻塞
      FreeList &list = free_lists_[FreeListIndex(aligned_size)];
      LockGuard guard(list.mutex);
      auto *q = reinterpret_cast<MemBlock *>(p);
      q->next = list.head;
      list.head = q;
    }

    // 当前池向系统申请的总字节数（线程安全，内部加锁）
    [[nodiscard]] size_t heap_size() const noexcept {
      LockGuard guard(chunk_mutex_);
      return heap_size_;
    }

  private:
    // 向系统申请内存并分配给空闲链表（慢路径）。
    //
    // 加锁策略：先取 chunk 锁（保护游标与 chunk 链），再取目标桶锁。
    // 本函数只会在调用方**未持有任何桶锁**时进入（Allocate 的快路径在桶锁作用域内
    // 直接返回，未命中时已释放桶锁），因此「先 chunk 后桶」的顺序天然成立。
    [[nodiscard]] void *Refill(size_t n) {
      int nobjs = Config::kDefaultNobjs;

      char *chunk = ChunkAlloc(n, nobjs);
      assert(chunk != nullptr);

      if (nobjs == 1) {
        return chunk;
      }

      // 把剩余的 nobjs-1 个块串成链表挂到本桶（仅在此处取目标桶锁）
      FreeList &list = free_lists_[FreeListIndex(n)];
      LockGuard guard(list.mutex);

      MemBlock *result = reinterpret_cast<MemBlock *>(chunk);
      MemBlock *current = reinterpret_cast<MemBlock *>(chunk + n);
      list.head = current;

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

    // 向系统申请大块内存（调用方不得持有任何桶锁；内部取 chunk 锁）
    [[nodiscard]] char *ChunkAlloc(size_t size, int &nobjs) {
      LockGuard guard(chunk_mutex_);
      return ChunkAllocLocked(size, nobjs);
    }

    // ChunkAlloc 的实现体：**调用方必须已持有 chunk_mutex_**。
    // 拆出这一层是因为 std::mutex 不可重入，而本函数在申请新 chunk 后需要重新
    // 走一遍分配逻辑——若直接递归公开入口就会再次取同一把锁而自锁。
    [[nodiscard]] char *ChunkAllocLocked(size_t size, int &nobjs) {

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
          // 旧 chunk 的残量归还到对应桶：需要短暂取该桶锁。
          // 顺序仍为「先 chunk 后桶」（chunk 锁已持有），与 Refill 一致，不会死锁。
          const size_t leftover = bytes_left;
          FreeList &leftover_list = free_lists_[FreeListIndex(leftover)];
          {
            LockGuard leftover_guard(leftover_list.mutex);
            reinterpret_cast<MemBlock *>(start_free_)->next = leftover_list.head;
            leftover_list.head = reinterpret_cast<MemBlock *>(start_free_);
          }
        }

        char *new_chunk = reinterpret_cast<char *>(std::malloc(bytes_to_get));
        if (new_chunk == nullptr) {
          // 向系统申请失败：退而扫描各桶找可用内存，逐个尝试（每次只持一个桶锁）
          for (size_t i = size; i <= Config::kMaxSmallObjectBytes; i += Config::kAlignSize) {
            FreeList &list = free_lists_[FreeListIndex(i)];
            LockGuard list_guard(list.mutex);
            MemBlock *p = list.head;
            if (p != nullptr) {
              list.head = p->next;
              // 注意：这里直接返回该块，不再递归 ChunkAlloc——
              // 递归会在已持有 chunk 锁的情况下重复获取，虽为同一把锁也会死锁。
              nobjs = 1;
              return reinterpret_cast<char *>(p);
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
        return ChunkAllocLocked(size, nobjs);
      }
    }
  };

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_SECOND_LEVEL_HPP_
