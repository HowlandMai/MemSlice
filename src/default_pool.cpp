// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#include "memory_pool/pool.hpp"

#include <cstdlib>
#include <new>

namespace memory_pool {

  // 全局内存池：函数内 static + placement new 的泄漏式单例。
  // 池本体建于系统堆上（placement new 不走被重载的全局 operator new，避免递归初始化），
  // 刻意永不析构（泄漏）以彻底消除两类全局生命周期问题：
  //  1. 构造顺序：其它 TU 的全局对象构造中执行 new 时，池一定已就绪（首次调用即构造）；
  //  2. 析构顺序：池永不析构，其它全局对象析构中执行 delete 不会撞上已释放的池。
  MemoryPool<> &DefaultMemoryPool() {
    static MemoryPool<> *pool = [] {
      void *storage = std::malloc(sizeof(MemoryPool<>));
      if (storage == nullptr) {
        throw std::bad_alloc();
      }
      return new (storage) MemoryPool<>();
    }();
    return *pool;
  }

} // namespace memory_pool
