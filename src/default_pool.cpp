// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

#include "mempool_lab/pool.hpp"

namespace mempool_lab {

  MemoryPool<DefaultConfig> &DefaultPool() noexcept {
    // 刻意不析构：析构顺序问题会让"最后释放的静态对象"踩到已销毁的池。
    // 让进程退出时由操作系统回收，是分配器领域的常规做法。
    static MemoryPool<DefaultConfig> *pool = new MemoryPool<DefaultConfig>();
    return *pool;
  }

} // namespace mempool_lab
