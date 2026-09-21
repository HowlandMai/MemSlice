// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

#ifndef MEMPOOL_LAB_FREE_LIST_HPP_
#define MEMPOOL_LAB_FREE_LIST_HPP_

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <type_traits>

#include "block.hpp"
#include "config.hpp"

namespace mempool_lab {

  // 二级分配器：按块容量分桶的空闲链表。
  //
  // 工作原理：
  //   1. 分配时按容量定位桶，从链表头摘一个节点（O(1)）
  //   2. 桶空时向系统批量申请一大块（chunk），切分成等长块串成链表
  //   3. 释放时把块挂回对应桶的链表头（O(1)）
  //
  // 并发策略（分桶加锁）：每个桶一把锁，不同尺寸互不阻塞。
  // 这是刻意的取舍——正确性容易论证，但单线程下每次操作都要加解锁。
  // 更优的解法是 thread-local 缓存，但那会引入跨线程归还的所有权问题，
  // 本项目不实现（见 docs/04-performance-notes.md）。
  template<typename Config>
  class FreeListPool {
  private:
    static constexpr std::size_t kNumBuckets = Config::kMaxBlockBytes / Config::kAlignSize;

    // 单线程配置下用空操作锁，避免任何加解锁开销
    struct NoopLock {
      void lock() noexcept {}
      void unlock() noexcept {}
    };
    using MutexT = std::conditional_t<Config::kThreadSafe, std::mutex, NoopLock>;
    using LockGuard = std::lock_guard<MutexT>;

    // 一个桶：链表头 + 自己的锁。
    // 每桶独立结构体，天然分散到不同 cache line，减少伪共享。
    struct Bucket {
      FreeNode *head = nullptr;
      mutable MutexT mutex;
    };

    Bucket buckets_[kNumBuckets]{};

    // ---- 以下为 chunk 游标，统一由 chunk_mutex_ 保护 ----
    char *chunk_begin_ = nullptr; // 当前 chunk 中尚未切分的起点
    char *chunk_end_ = nullptr; // 当前 chunk 的末尾

    // 已申请 chunk 的登记链表（析构时统一释放）
    struct ChunkRecord {
      char *base;
      std::size_t size;
      ChunkRecord *next;
    };
    ChunkRecord *chunks_ = nullptr;
    std::size_t total_bytes_ = 0;

    mutable MutexT chunk_mutex_;

    [[nodiscard]] static constexpr std::size_t RoundUp(std::size_t bytes) noexcept {
      if (bytes == 0) {
        return Config::kAlignSize; // 0 字节按最小粒度处理，避免 (0-1) 下溢
      }
      return (bytes + Config::kAlignSize - 1) & ~(Config::kAlignSize - 1);
    }

    [[nodiscard]] static constexpr std::size_t BucketOf(std::size_t bytes) noexcept {
      return (RoundUp(bytes) - 1) / Config::kAlignSize;
    }

  public:
    static constexpr std::size_t kMinBytes = Config::kAlignSize;
    static constexpr std::size_t kMaxBytes = Config::kMaxBlockBytes;

    FreeListPool() { static_assert(detail::ValidConfig<Config>()); }

    ~FreeListPool() {
      ChunkRecord *r = chunks_;
      while (r != nullptr) {
        std::free(r->base);
        ChunkRecord *cur = r;
        r = r->next;
        std::free(cur);
      }
      chunks_ = nullptr;
    }

    FreeListPool(const FreeListPool &) = delete;
    FreeListPool &operator=(const FreeListPool &) = delete;

    // 该块容量是否可由本分配器承担
    [[nodiscard]] static constexpr bool CanServe(std::size_t block_bytes) noexcept {
      return block_bytes >= kMinBytes && block_bytes <= kMaxBytes;
    }

    [[nodiscard]] static constexpr std::size_t NumBuckets() noexcept { return kNumBuckets; }

    // 某桶对应的块容量（审计用）
    [[nodiscard]] static constexpr std::size_t BucketCapacity(std::size_t bucket) noexcept {
      return (bucket + 1) * Config::kAlignSize;
    }

    // 按块容量分配。容量须落在 [kMinBytes, kMaxBytes]，否则抛 std::bad_alloc。
    [[nodiscard]] void *Allocate(std::size_t block_bytes) {
      const std::size_t aligned = RoundUp(block_bytes);
      if (aligned > kMaxBytes) {
        throw std::bad_alloc();
      }
      const std::size_t idx = BucketOf(aligned);

      // 快路径：从本桶摘一个
      {
        Bucket &b = buckets_[idx];
        LockGuard guard(b.mutex);
        if (FreeNode *node = b.head; node != nullptr) {
          b.head = node->next;
          return node;
        }
      }

      // 慢路径：本桶空，向 chunk 要内存
      return Refill(aligned);
    }

    // 按块容量回收。容量须与分配时一致（由上层头部保证）。
    void Deallocate(void *p, std::size_t block_bytes) noexcept {
      if (p == nullptr) {
        return;
      }
      const std::size_t aligned = RoundUp(block_bytes);
      if (aligned > kMaxBytes) {
        // 尺寸越界：无法判断该块属于哪个桶（否则会越界索引 buckets_）。
        // 这是"不变量被破坏"的信号——正常路径不该发生（上层头部记录了真实容量）。
        assert(false && "Deallocate: 块容量越界，无法定位桶");
        return;
      }
      Bucket &b = buckets_[BucketOf(aligned)];
      LockGuard guard(b.mutex);
      auto *node = static_cast<FreeNode *>(p);
      node->next = b.head;
      b.head = node;
    }

    // 池向系统申请的总字节数
    [[nodiscard]] std::size_t total_bytes() const noexcept {
      LockGuard guard(chunk_mutex_);
      return total_bytes_;
    }

    // 遍历某桶（只读，供审计使用）。fn(const FreeNode*, bucket)
    template<typename Fn>
    void ForEachInBucket(std::size_t bucket, Fn &&fn) const {
      const Bucket &b = buckets_[bucket];
      LockGuard guard(b.mutex);
      for (const FreeNode *n = b.head; n != nullptr; n = n->next) {
        fn(n);
      }
    }

    // 收集已申请 chunk 的地址区间（供审计判断节点归属）
    template<typename Fn>
    void ForEachChunk(Fn &&fn) const {
      LockGuard guard(chunk_mutex_);
      for (const ChunkRecord *r = chunks_; r != nullptr; r = r->next) {
        fn(r->base, r->size);
      }
    }

  private:
    // 向系统申请 chunk，切分成块，返回一个给调用方，其余挂入本桶。
    [[nodiscard]] void *Refill(std::size_t size) {
      char *chunk = nullptr;
      // count 是**入参兼出参**：传入「本次希望切出多少个块」，返回实际切出的个数。
      // ★ 必须初始化为期望值（kBatchSize），不能为 0——若传 0，则 ChunkAlloc 里
      //   want = count * size = 0，「当前 chunk 足够」的分支会直接返回而不修改 count，
      //   于是 count 仍是 0，后面的串链循环 count - 1 下溢成 SIZE_MAX，
      //   沿内存一路写出去造成段错误。（这是本项目踩过的真实缺陷。）
      std::size_t count = Config::kBatchSize;
      {
        LockGuard guard(chunk_mutex_);
        chunk = ChunkAlloc(size, count);
      }
      // 契约：chunk 已确保能容纳 count 个 size 大小的块，且 count >= 1
      assert(count >= 1 && "ChunkAlloc 必须至少返回一个块");

      if (count == 1) {
        return chunk;
      }

      // 把其余的块串成链表挂到本桶。
      // 顺序：先算好整条链，再加锁挂入——减少持锁时间。
      Bucket &b = buckets_[BucketOf(size)];
      LockGuard guard(b.mutex);

      auto *first = reinterpret_cast<FreeNode *>(chunk);
      auto *current = reinterpret_cast<FreeNode *>(chunk + size);
      b.head = current;

      for (std::size_t i = 1;; ++i) {
        if (i == count - 1) {
          current->next = nullptr;
          break;
        }
        current->next = reinterpret_cast<FreeNode *>(reinterpret_cast<char *>(current) + size);
        current = current->next;
      }
      return first;
    }

    // 从当前 chunk 切出 count 个 size 大小的块；不够则申请新 chunk。
    // 调用方必须已持有 chunk_mutex_。
    [[nodiscard]] char *ChunkAlloc(std::size_t size, std::size_t &count) {
      // 前置条件：count 是「希望切出多少个块」，必须 >= 1。
      // 若调用方误传 0，want 会是 0，本函数会走进「chunk 够用」分支直接返回，
      // 而 count 保持 0 —— 调用方随后按 count 串链时会下溢。故在此显式兜底。
      assert(count >= 1 && "ChunkAlloc: count 必须 >= 1（入参语义为期望块数）");
      if (count == 0) {
        count = 1;
      }
      const std::size_t want = count * size;

      const std::size_t left = (chunk_begin_ == nullptr) ? 0 : static_cast<std::size_t>(chunk_end_ - chunk_begin_);

      if (left >= want) {
        // 当前 chunk 够用（count 保持不变——它就是实际切出的个数）
        char *result = chunk_begin_;
        chunk_begin_ += want;
        return result;
      }

      if (left >= size) {
        // 只够一部分：尽量多切
        count = left / size;
        char *result = chunk_begin_;
        chunk_begin_ += count * size;
        return result;
      }

      // 需要新 chunk。先把残料归入对应桶（避免浪费）——
      // 注意残料大小必须是 kAlignSize 的整数倍，否则不能作为块复用。
      if (left > 0 && (left % Config::kAlignSize == 0)) {
        Bucket &b = buckets_[BucketOf(left)];
        LockGuard guard(b.mutex);
        auto *node = reinterpret_cast<FreeNode *>(chunk_begin_);
        node->next = b.head;
        b.head = node;
      }

      // 申请新 chunk：2 倍需求 + 一点额外余量，摊薄后续申请
      const std::size_t bytes_to_get = 2 * want + RoundUp(total_bytes_ >> 4);
      char *fresh = static_cast<char *>(std::malloc(bytes_to_get));
      if (fresh == nullptr) {
        throw std::bad_alloc();
      }

      auto *record = static_cast<ChunkRecord *>(std::malloc(sizeof(ChunkRecord)));
      if (record == nullptr) {
        std::free(fresh);
        throw std::bad_alloc();
      }
      record->base = fresh;
      record->size = bytes_to_get;
      record->next = chunks_;
      chunks_ = record;
      total_bytes_ += bytes_to_get;

      chunk_begin_ = fresh;
      chunk_end_ = fresh + bytes_to_get;

      // 递归切分（此时已持有 chunk_mutex_，故不能调用加锁的公开入口）
      return ChunkAlloc(size, count);
    }
  };

} // namespace mempool_lab

#endif // MEMPOOL_LAB_FREE_LIST_HPP_
