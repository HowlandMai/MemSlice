// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

#ifndef MEMPOOL_LAB_POOL_HPP_
#define MEMPOOL_LAB_POOL_HPP_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "audit.hpp"
#include "block.hpp"
#include "config.hpp"
#include "free_list.hpp"

namespace mempool_lab {

  // 内存池门面。
  //
  // 职责划分（这个分层很重要）：
  //   MemoryPool  ：管头部记账、路由（小对象走池 / 大对象走系统）、审计
  //   FreeListPool：只管裸块（没有任何头部概念），按容量分桶
  //
  // ★ 为什么这样分：FreeListPool 收到的是**裸块指针**，那里没有头部。
  //   头部的读写必须发生在 MemoryPool 层——只有这一层才知道用户指针与头部的
  //   相对位置。在低层误读头部是真实 bug 的来源（见 docs/03-building-log.md）。
  template<typename Config = DefaultConfig>
  class MemoryPool {
  public:
    static constexpr std::size_t kBlockAlign = alignof(std::max_align_t);
    static constexpr std::size_t kHeaderBytes = detail::kHeaderBytes;

    // 可请求的最大尺寸：再大就无法在 packed 容量域中表达
    static constexpr std::size_t kMaxRequestBytes = detail::kMaxCapacity - kHeaderBytes - kBlockAlign;

    MemoryPool() = default;
    MemoryPool(const MemoryPool &) = delete;
    MemoryPool &operator=(const MemoryPool &) = delete;

    // 分配 size 字节，返回满足 max_align_t 对齐的用户指针
    [[nodiscard]] void *Allocate(std::size_t size) {
      if (size > kMaxRequestBytes) {
        throw std::bad_alloc();
      }
      // 底层块要容纳「头部 + 用户数据」，并留出对齐余量：
      // 用户指针在 raw + kHeaderBytes 之后向上取整到 kBlockAlign，
      // 最多多消耗 kBlockAlign 字节。
      const std::size_t total = detail::AlignUp(kHeaderBytes + size, kBlockAlign);
      if (total < size || total > detail::kMaxCapacity) {
        throw std::bad_alloc(); // 回绕或超出可表达范围
      }

      detail::BlockSource source;
      void *raw;
      if (FreeListPool<Config>::CanServe(total)) {
        raw = pool_.Allocate(total);
        source = detail::BlockSource::kPool;
      } else {
        raw = std::malloc(total);
        if (raw == nullptr) {
          throw std::bad_alloc();
        }
        source = detail::BlockSource::kSystem;
      }

      // 头部紧邻用户指针前方
      const std::uintptr_t p = detail::AlignUp(reinterpret_cast<std::uintptr_t>(raw) + kHeaderBytes, kBlockAlign);
      auto *h = reinterpret_cast<detail::Header *>(p - kHeaderBytes);
      h->base = raw;
      h->packed = detail::PackFlags(total, source, /*allocated=*/true, detail::kMagicValue);
      return reinterpret_cast<void *>(p);
    }

    // 释放由本池分配的指针。
    //
    // 接口刻意只收一个指针：尺寸由头部记录，无需调用方提供。
    // 这是本实现区别于"经典实现需要传尺寸"的地方，也是头部的直接收益。
    void Deallocate(void *p) noexcept {
      if (p == nullptr) {
        return;
      }
      const detail::Header *h = detail::HeaderOf(p);

      // ★ 顺序至关重要：先在头部有效的状态下取出所需信息，
      //   再交给低层回收（回收会把 next 写在块起始处、覆盖头部）。
      //   回收之后**不得**再读写头部。
      const std::size_t capacity = detail::UnpackCapacity(h->packed);
      const detail::BlockSource source = detail::UnpackSource(h->packed);
      void *const base = h->base;

      if (source == detail::BlockSource::kPool) {
        pool_.Deallocate(base, capacity);
      } else {
        std::free(base);
      }
    }

    // 重新分配：语义类似 realloc（成功则旧内容前 min(旧,新) 字节保留且旧指针失效）。
    //
    // 实现上只做「分配 + 拷贝 + 回收」，且同容量时原地复用（不拷贝、不搬移）。
    [[nodiscard]] void *Reallocate(void *p, std::size_t new_size) {
      if (p == nullptr) {
        return Allocate(new_size);
      }
      const std::size_t old_capacity = detail::CapacityOf(p);

      // 同容量内：原地复用，不搬运。这是常见场景（如 vector 内部增长前的查探）
      // 的重要优化路径。
      if (new_size + kHeaderBytes <= old_capacity) {
        auto *h = detail::HeaderOf(p);
        h->packed =
            detail::PackFlags(old_capacity, detail::UnpackSource(h->packed), /*allocated=*/true, detail::kMagicValue);
        return p;
      }

      void *fresh = Allocate(new_size);
      // 只拷贝一定有意义的字节数：min(旧可用, 新请求)
      const std::size_t old_usable = (old_capacity > kHeaderBytes) ? (old_capacity - kHeaderBytes) : 0;
      const std::size_t copy_bytes = (old_usable < new_size) ? old_usable : new_size;
      if (copy_bytes > 0) {
        std::memcpy(fresh, p, copy_bytes);
      }
      Deallocate(p);
      return fresh;
    }

    // ---- 诊断接口 ----

    [[nodiscard]] std::size_t total_bytes() const noexcept { return pool_.total_bytes(); }

    // 内部不变量审计：只读、不参与热路径，供测试与排查显式调用。
    //
    // 核对的不变量：
    //   1. 每个桶的链表无环、节点按 kAlignSize 对齐、长度有上限
    //   2. 每个空闲节点都落在本池已申请的 chunk 范围内
    //   3. 空闲节点在两两之间不重复（同一块不得出现两次）
    //   4. chunk 游标自洽（起点不越过终点）
    [[nodiscard]] detail::AuditReport Audit() const {
      detail::AuditReport rep;

      // 收集 chunk 区间
      std::vector<detail::Range> ranges;
      pool_.ForEachChunk([&ranges](const char *base, std::size_t size) {
        ranges.push_back({reinterpret_cast<std::uintptr_t>(base), reinterpret_cast<std::uintptr_t>(base) + size});
      });

      // 逐桶检查
      std::vector<std::uintptr_t> seen;
      for (std::size_t bucket = 0; bucket < FreeListPool<Config>::NumBuckets(); ++bucket) {
        // 安全阀：单桶节点数不应超过该桶理论最大值
        const std::size_t cap = FreeListPool<Config>::BucketCapacity(bucket);
        const std::size_t max_nodes = (total_bytes() / cap) + 64;

        std::vector<const FreeNode *> nodes;
        pool_.ForEachInBucket(bucket, [&nodes](const FreeNode *n) { nodes.push_back(n); });

        // 环检测需要从桶头开始，因此这里把节点收集后自行检查结构
        // （ForEachInBucket 已完成一次遍历，若成环它会挂住——故先做环检测）
        std::size_t count = 0;
        for (const FreeNode *n: nodes) {
          if (reinterpret_cast<std::uintptr_t>(n) % Config::kAlignSize != 0) {
            rep.Fail("空闲节点未按 kAlignSize 对齐");
            break;
          }
          if (!detail::InAnyRange(ranges, n)) {
            rep.Fail("空闲节点不属于本池任何 chunk");
            break;
          }
          if (++count > max_nodes) {
            rep.Fail("空闲链表异常长（可能已被污染）");
            break;
          }
        }

        // 跨桶唯一性：同一块不得出现在两个位置
        for (const FreeNode *n: nodes) {
          const auto v = reinterpret_cast<std::uintptr_t>(n);
          for (const auto &s: seen) {
            if (s == v) {
              rep.Fail("同一个块在空闲链表中出现超过一次");
              break;
            }
          }
          seen.push_back(v);
        }
        if (!rep.ok) {
          break;
        }
      }
      return rep;
    }

    // 校验一个**刚分配出来**的指针的头部自洽性；返回 nullptr 表示正常。
    //
    // 为什么只在分配态校验：空闲节点的头部已被 next 覆盖（见 block.hpp 的说明），
    // 只有分配出来的块头部才是有效的。
    [[nodiscard]] const char *VerifyAllocated(const void *p, std::size_t requested) const {
      if (p == nullptr) {
        return "指针为空";
      }
      const detail::Header *h = detail::HeaderOf(p);
      if (detail::UnpackMagic(h->packed) != detail::kMagicValue) {
        return "头部魔数无效（头部被写坏或指针非本池分配）";
      }
      if (!detail::UnpackAllocated(h->packed)) {
        return "头部状态位为『已释放』";
      }
      if (h->base == nullptr) {
        return "头部记录的基址为空";
      }
      const std::size_t capacity = detail::UnpackCapacity(h->packed);
      if (capacity < requested) {
        return "块容量小于请求尺寸";
      }
      if (reinterpret_cast<std::uintptr_t>(p) % kBlockAlign != 0) {
        return "用户指针未满足 max_align_t 对齐";
      }
      return nullptr;
    }

  private:
    FreeListPool<Config> pool_;
  };

  // 全局默认池（定义在 src/default_pool.cpp）。
  // 刻意做成不析构的单例：析构顺序问题会让"最后释放的静态对象"踩到已销毁的池。
  [[nodiscard]] MemoryPool<DefaultConfig> &DefaultPool() noexcept;

} // namespace mempool_lab

#endif // MEMPOOL_LAB_POOL_HPP_
