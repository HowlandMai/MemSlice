// 单线程分配路径的性能剖析目标。
//
// 用途：用指令级计数（callgrind）回答「池为何不比 malloc 快」——
// 时间测量只能给出比值，这里给出**每条路径每对 alloc/free 的指令数**，
// 从而把开销归因到具体函数（锁 / 核心逻辑 / 调用方）。
//
// 用法：
//   xmake run prof_memory_pool pool   20000 32
//   xmake run prof_memory_pool malloc 20000 32
//   valgrind --tool=callgrind --callgrind-out-file=/tmp/cg.out \
//            ./build/linux/x86_64/release/prof_memory_pool pool 20000 32
//
// 结论（本机 x86-64 / clang -O2 / glibc 2.43，见 README 性能说明）：
//   池 235 指令/对，其中锁占 105（45%）；核心逻辑 117；调用方 13。
//   系统 malloc+free 57 指令/对。
//   去掉锁后池核心逻辑与 malloc 同量级——瓶颈是「每次分配/释放各取一次锁」，
//   而非分桶空闲链表本身。

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "memory_pool/pool.hpp"

using namespace memory_pool;

// volatile 汇聚点：阻止优化器把分配/释放整段消除
volatile void *g_prof_sink = nullptr;

// 场景 A：只用内存池（预热后进入稳态，避免把首次 chunk 申请计入）
int RunPool(int iters, size_t size) {
  MemoryPool<> pool;
  for (int i = 0; i < 1000; ++i) {
    void *p = pool.Allocate(size);
    pool.Deallocate(p);
  }
  for (int i = 0; i < iters; ++i) {
    void *p = pool.Allocate(size);
    g_prof_sink = p;
    pool.Deallocate(p);
  }
  return static_cast<int>(pool.heap_size() & 1);
}

// 场景 B：只用系统 malloc（作为对照基线）
int RunMalloc(int iters, size_t size) {
  for (int i = 0; i < 1000; ++i) {
    void *p = std::malloc(size);
    std::free(p);
  }
  for (int i = 0; i < iters; ++i) {
    void *p = std::malloc(size);
    g_prof_sink = p;
    std::free(p);
  }
  return 0;
}

int main(int argc, char **argv) {
  const std::string mode = (argc > 1) ? argv[1] : "pool";
  const int iters = (argc > 2) ? std::atoi(argv[2]) : 20000;
  const size_t size = (argc > 3) ? static_cast<size_t>(std::atoi(argv[3])) : 32;

  if (mode == "pool") {
    return RunPool(iters, size);
  }
  if (mode == "malloc") {
    return RunMalloc(iters, size);
  }
  std::printf("usage: %s pool|malloc [iters] [size]\n", argv[0]);
  return 2;
}
