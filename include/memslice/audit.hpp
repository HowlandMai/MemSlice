// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

#ifndef MEMSLICE_AUDIT_HPP_
#define MEMSLICE_AUDIT_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "block.hpp"

namespace memslice::detail {

  // 内部不变量审计。
  //
  // 设计动机：一个"看起来能跑"的池和一个"能证明自己是对的"池，差别就在于
  // 前者出错时表现为段错误（不告诉你是哪条不变量坏了），后者能当场指出。
  //
  // ★ 写审计时必须注意的两条（都来自实战教训）：
  //
  //   1. 只检查**真正可验证**的不变量。曾写过两条错误的检查：
  //      - 要求空闲节点 16 字节对齐（错：池按 kAlignSize 切块，只保证 8 对齐）
  //      - 要求链表相邻节点地址差等于桶容量（错：链表相邻性不要求地址等差）
  //      错误的检查制造误报，把真问题淹没。
  //
  //   2. 审计自身绝不能挂住或崩溃。遍历有环链表会死循环，因此**必须先做环检测**，
  //      再遍历，并带长度安全阀。

  struct AuditReport {
    bool ok = true;
    std::vector<const char *> violations;

    void Fail(const char *what) {
      ok = false;
      violations.push_back(what);
    }
  };

  // 链表结构检查（不涉及块容量语义，故可安全使用）
  struct ChainInfo {
    std::size_t length = 0;
    bool cyclic = false;
    bool misaligned = false;
    bool overlong = false;
  };

  // max_nodes 是安全阀：即便链表被破坏成超长链，审计也必须有限步结束。
  [[nodiscard]] inline ChainInfo InspectChain(const FreeNode *head, std::size_t align_required,
                                              std::size_t max_nodes) noexcept {
    ChainInfo info;

    // 先查环（Floyd 快慢指针）——顺序关键，否则有环时遍历会死循环
    {
      const FreeNode *slow = head;
      const FreeNode *fast = head;
      while (fast != nullptr && fast->next != nullptr) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) {
          info.cyclic = true;
          break;
        }
      }
    }
    if (info.cyclic) {
      return info;
    }

    for (const FreeNode *n = head; n != nullptr; n = n->next) {
      if (reinterpret_cast<std::uintptr_t>(n) % align_required != 0) {
        info.misaligned = true;
        break;
      }
      if (++info.length > max_nodes) {
        info.overlong = true;
        break;
      }
    }
    return info;
  }

  // 地址是否落在一组半开区间 [begin, end) 内
  struct Range {
    std::uintptr_t begin;
    std::uintptr_t end;
  };

  [[nodiscard]] inline bool InAnyRange(const std::vector<Range> &ranges, const void *p) noexcept {
    const auto v = reinterpret_cast<std::uintptr_t>(p);
    for (const auto &r: ranges) {
      if (v >= r.begin && v < r.end) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] inline bool InAnyRangeClosed(const std::vector<Range> &ranges, const void *p) noexcept {
    const auto v = reinterpret_cast<std::uintptr_t>(p);
    for (const auto &r: ranges) {
      if (v >= r.begin && v <= r.end) {
        return true;
      }
    }
    return false;
  }

} // namespace memslice::detail

#endif // MEMSLICE_AUDIT_HPP_
