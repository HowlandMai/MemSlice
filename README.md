# mempool-lab

一个**教学用途**的两级内存池实现。目标不是替代 `glibc malloc`，而是
**把分配器的设计原理讲清楚，并展示如何证明一个系统级组件是正确的**。

> ### ⚠️ 先看这条
>
> **本实现单线程下比 `glibc malloc` 慢约 3.7 倍**（实测 13.2 vs 3.5 ns/op）。
> 这是分桶加锁设计的直接代价——glibc 的 `tcache` 是无锁快路径，而本实现每次
> 分配/释放都要加解锁。
>
> **性能敏感场景请直接用 `glibc malloc`。** 本项目适合阅读、学习、改造。
>
> 完整的实测数据与归因见 [`docs/04-performance-notes.md`](docs/04-performance-notes.md)。

## 它解决什么问题

`malloc` 是通用分配器，必须处理任意尺寸、任意释放顺序、线程安全，
因此每次分配都要做不少工作。如果程序的分配模式可预测（大量同尺寸小对象、
频繁申请释放），可以针对性设计来省掉这些开销。

本实现采用经典的两级结构：

```
请求 → size <= 128B ? ─是→ 二级分配器（按尺寸分桶 + 空闲链表，O(1)）
                      └─否→ 一级分配器（直接 malloc/free）
```

## 核心特性

| 特性 | 说明 |
| --- | --- |
| **两级分配** | 小对象走池、大对象走系统，各取所长 |
| **自描述头部** | `Deallocate(p)` 单参，尺寸由头部记录——接口比"调用方传尺寸"更安全 |
| **O(1) 分配/释放** | 分桶定位 + 链表头操作，无搜索 |
| **批量申请 chunk** | 一次向系统要一批，摊薄系统调用 |
| **分桶加锁** | 不同尺寸的并发操作互不阻塞 |
| **内部不变量审计** | `Audit()` 让池能自证账目是否自洽 |
| **STL 分配器** | `Allocator<T>` 可直接用于 `std::vector` / `std::list` / `std::map` |

## 快速上手

```cpp
#include "mempool_lab/pool.hpp"

int main() {
  mempool_lab::MemoryPool<> pool;

  void *p = pool.Allocate(64);
  // ... 使用 p ...
  pool.Deallocate(p);                 // 无需传尺寸

  void *q = pool.Reallocate(p, 128);  // 同容量内原地复用，否则搬移
  pool.Deallocate(q);

  // 审计：核对池内部账目
  const auto report = pool.Audit();
  if (!report.ok) {
    for (const char *v : report.violations) {
      std::fprintf(stderr, "池账目异常: %s\n", v);
    }
  }
}
```

与标准容器配合：

```cpp
#include "mempool_lab/allocator.hpp"

std::vector<int, mempool_lab::Allocator<int>> v;
std::list<std::string, mempool_lab::Allocator<std::string>> l;

// 超对齐类型也支持（内部回落到系统分配以记录偏移，见文档）
struct alignas(64) Vec4 { float v[4]; };
std::vector<Vec4, mempool_lab::Allocator<Vec4>> simd;
```

## 构建与测试

需要：`xmake`、支持 C++20 的 `clang`。

```bash
xmake                        # 构建
xmake test                   # 跑测试（门禁）
./build/linux/x86_64/release/test_mempool_lab          # 直接运行
./build/linux/x86_64/release/test_mempool_lab basic    # 只跑 basic 组
./build/linux/x86_64/release/test_mempool_lab --quiet  # 安静模式
```

全量门禁（用例 + Sanitizer + valgrind + 告警 + 格式）：

```bash
bash scripts/ci.sh
```

基准（对比本池与 glibc malloc）：

```bash
xmake build bench_mempool_lab
./build/linux/x86_64/release/bench_mempool_lab
```

## 文档（本项目的主要产出）

| 文档 | 内容 |
| --- | --- |
| [00 · 总览](docs/00-overview.md) | 项目要讲什么、为什么这样组织、与原版实现的关系 |
| [01 · 原理](docs/01-principles.md) | 为什么这样设计（从"不用它会怎样"出发，配 ASCII 图） |
| [02 · 设计权衡](docs/02-design-choices.md) | **每个选择放弃了什么**——比列优点更有价值 |
| [03 · 构建日志](docs/03-building-log.md) | **实际踩到的缺陷**：症状→定位→根因→修法 |
| [04 · 性能分析](docs/04-performance-notes.md) | 实测数据与归因（为什么打不过 glibc） |
| [05 · 验证方法](docs/05-verification.md) | **如何证明一个池是对的**——六层验证体系 |
| [06 · STL 分配器](docs/06-allocator.md) | 接入标准容器；相等性语义与超对齐两个坑 |

建议阅读顺序：01 → 02 → 05 → 03 → 04。

## 目录结构

```
mempool-lab/
├── xmake.lua                      # 构建（xmake + clang，C++20）
├── include/mempool_lab/
│   ├── config.hpp                 # 配置参数与编译期校验
│   ├── block.hpp                  # 头部布局、位域编解码、对齐工具
│   ├── free_list.hpp              # 二级分配器：分桶空闲链表 + chunk 管理
│   ├── audit.hpp                  # 不变量审计的原语
│   ├── allocator.hpp              # STL 分配器适配（含超对齐处理）
│   └── pool.hpp                   # 门面：路由、头部记账、审计
├── src/default_pool.cpp           # 全局池单例
├── test/
│   ├── test_utils.hpp             # 零依赖断言与用例注册
│   ├── test_basic.cpp             # 基础功能与边界
│   ├── test_realloc.cpp           # 重新分配的数据保全
│   ├── test_audit.cpp             # 审计：防误报 + 防漏报
│   ├── test_fuzz.cpp              # 随机长跑与指针唯一性
│   ├── test_concurrency.cpp       # 多线程与跨线程归还
│   ├── test_allocator.cpp         # STL 分配器：容器、rebind、超对齐
├── bench/bench_lab.cpp            # 简易基准
├── docs/                          # 学习文档（见上表）
└── scripts/ci.sh                  # 全量门禁
```

**分层原则**：`MemoryPool` 管头部记账与路由；`FreeListPool` 只处理**裸块**，
完全不碰头部。这个划分让"哪一层能访问什么"成为明确约束——它直接来自一次
真实缺陷的修复（见 [构建日志 §2](docs/03-building-log.md#2-头部与链表指针重叠)）。

## 与原版实现的关系（诚实声明）

核心算法（两级分配器、按尺寸分桶、空闲链表复用块内存、边界标记）
**来自经典设计，与 SGI STL 的 `__default_alloc_template` 思路一致**。
本项目不声称算法原创。

差异在于：

1. **自描述头部**：`Deallocate(p)` 单参，无需调用方提供尺寸；
   同时能区分块的来源（池块 / 系统块）
2. **现代 C++ 表达**：模板配置、`if constexpr` 零开销开关、`constexpr` 推导
3. **可验证性体系**：不变量审计 + 注入错误测试（原版没有这一层）
4. **诚实的性能边界**：明确标注"打不过 glibc"，并给出量化归因

> 本项目想展示的不是"我发明了分配算法"，而是
> **"我理解它的每个取舍，能证明我的实现是对的，并且知道它的边界在哪"**。

## 已知边界与不做的事

**性能**：单线程慢于 glibc malloc（见上）。要提速需要线程本地缓存，
但那会引入跨线程归还的所有权问题——本项目刻意不实现
（讨论见 [04 §5](docs/04-performance-notes.md#5-如果要做快该怎么做也是不错的进阶练习)）。

**内存开销**：每块 16 字节头部；8 字节对齐取整。对 1~8 字节的对象开销占比过高。

**不做**：线程本地缓存、内存整理/合并、`mmap` 直连、超对齐支持、
可配置分配策略。理由见 [02 §6](docs/02-design-choices.md#6-刻意不做的功能)。

## 许可

MIT，见 [LICENSE](LICENSE)。
