// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

#ifndef MEMSLICE_POOL_HPP_
#define MEMSLICE_POOL_HPP_

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

namespace memslice {

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
    [[nodiscard]] void *Allocate(std::size_t size) { return AllocateAligned(size, kBlockAlign); }

    // 分配 size 字节，返回满足 alignment 对齐的用户指针。
    //
    // ★ 超对齐的关键：头部必须"贴"在用户指针前方，且 `p - kHeaderBytes` 必须
    //   落回头部。为此头部的实际占用是 **align_up(kHeaderBytes, alignment)**，
    //   而不是固定的 kHeaderBytes。若忽略这点（例如只按 max_align_t 取整），
    //   当 alignment=64 时 p - kHeaderBytes 会落在头部内部而不是头部起点，
    //   释放时读到垃圾容量 —— 曾实测段错误。
    //
    //   因此这里的做法是：把头部占位大小（head）按 alignment 取整，
    //   用户指针 p = align_up(raw + head, alignment) —— 由于 head 本身是
    //   alignment 的整数倍，且 raw 来自分配器（对齐足够），
    //   p - head 恰好落回 raw。
    [[nodiscard]] void *AllocateAligned(std::size_t size, std::size_t alignment) {
      if (size > kMaxRequestBytes) {
        throw std::bad_alloc();
      }
      const std::size_t align = (alignment < kBlockAlign) ? kBlockAlign : alignment;
      if ((align & (align - 1)) != 0) {
        throw std::bad_alloc(); // 对齐必须是 2 的幂
      }

      // 超对齐（align > kHeaderBytes）无法在池内正确处理：
      // 头部位置 p - head 中的 head 随 align 变化，而释放时只有 p，
      // 无法反推 head —— 除非额外记录（会使头部膨胀、并让所有路径变复杂）。
      // 因此对这种请求回落到系统分配（malloc 天然满足任何不超过其内部
      // 对齐策略的对齐要求；POSIX 下 malloc 至少 16 字节对齐，
      // 对更大的对齐我们显式多要字节并记录真实基址）。
      if (align > kHeaderBytes) {
        return AllocateOverAligned(size, align);
      }

      // 常规路径：head 固定为 kHeaderBytes（已是 kBlockAlign 的整数倍）
      const std::size_t total = kHeaderBytes + detail::AlignUp(size, align);
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
      // 头部紧邻用户指针前方（head == kHeaderBytes，固定值）
      const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(raw) + kHeaderBytes;
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
      // 超对齐路径的块：由前缀记录真实基址，与常规头部布局无关
      if (IsOverAligned(p)) {
        FreeOverAligned(p);
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
      // 超对齐块：不尝试原地复用（其布局不适用本路径），统一走分配+拷贝+回收
      if (IsOverAligned(p)) {
        void *fresh = Allocate(new_size);
        std::memcpy(fresh, p, new_size);
        FreeOverAligned(p);
        return fresh;
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
    // 超对齐分配：回落到系统分配，用一个"前缀"记录真实基址与偏移。
    //
    // 布局：  [前缀(16B): magic | size | offset] [填充] [用户指针 p]
    // 释放时 p 往前找到前缀即可还原 raw —— 因为偏移量被记录下来了。
    // 这份前缀只有超对齐路径才使用，常规路径零开销。
    struct OverAlignPrefix {
      std::uintptr_t magic;
      std::size_t offset; // raw 到 p 的距离
    };

    static constexpr std::uintptr_t kOverAlignMagic = 0x4F41'4C47'4E00'0001ull; // "OALGN"

    [[nodiscard]] static void *AllocateOverAligned(std::size_t size, std::size_t align) {
      const std::size_t prefix = detail::AlignUp(sizeof(OverAlignPrefix), alignof(std::max_align_t));
      // 多要 align 字节，保证其中一定有满足 align 的起点
      const std::size_t bytes = prefix + size + align;
      if (bytes < size) {
        throw std::bad_alloc();
      }
      void *raw = std::malloc(bytes);
      if (raw == nullptr) {
        throw std::bad_alloc();
      }
      const auto raw_addr = reinterpret_cast<std::uintptr_t>(raw);
      // 用户指针：在 raw + prefix 之后向上取整到 align
      const std::uintptr_t p = detail::AlignUp(raw_addr + prefix, align);
      // 前缀放在 p 前方紧邻处（保证可反推）
      auto *pfx = reinterpret_cast<OverAlignPrefix *>(p - sizeof(OverAlignPrefix));
      pfx->magic = kOverAlignMagic;
      pfx->offset = p - raw_addr;
      return reinterpret_cast<void *>(p);
    }

    [[nodiscard]] static bool IsOverAligned(const void *p) noexcept {
      const auto addr = reinterpret_cast<std::uintptr_t>(p);
      if (addr < sizeof(OverAlignPrefix)) {
        return false;
      }
      const auto *pfx = reinterpret_cast<const OverAlignPrefix *>(addr - sizeof(OverAlignPrefix));
      return pfx->magic == kOverAlignMagic;
    }

    static void FreeOverAligned(void *p) noexcept {
      const auto addr = reinterpret_cast<std::uintptr_t>(p);
      const auto *pfx = reinterpret_cast<const OverAlignPrefix *>(addr - sizeof(OverAlignPrefix));
      const std::uintptr_t raw = addr - pfx->offset;
      std::free(reinterpret_cast<void *>(raw));
    }

    FreeListPool<Config> pool_;
  };

  // 全局默认池（定义在 src/default_pool.cpp）。
  // 刻意做成不析构的单例：析构顺序问题会让"最后释放的静态对象"踩到已销毁的池。
  [[nodiscard]] MemoryPool<DefaultConfig> &DefaultPool() noexcept;

} // namespace memslice

#endif // MEMSLICE_POOL_HPP_
