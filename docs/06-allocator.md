# 06 · STL 分配器

本文讲如何把内存池接入标准库容器，以及其中最容易被忽略的两个坑。

## 1. 为什么需要单独一层

池提供的是 `Allocate(size)` / `Deallocate(p)`，而标准容器需要的是
**符合 Allocator 要求的类型**。两者接口不同，因此需要一层适配：

```cpp
std::vector<int, mempool_lab::Allocator<int>> v;   // 容器走池
```

这一层看起来只是接口搬运，实际有两个地方容易做错。

## 2. C++20 对 Allocator 的要求

C++17 到 C++20，要求**变少了**——很多成员有了默认推导。C++20 下核心要求：

| 要求 | 说明 |
| --- | --- |
| `value_type` | `allocator_traits` 依赖它做类型推导 |
| `allocate(n)` / `deallocate(p, n)` | 核心操作 |
| 可拷贝构造 | 容器按值持有分配器 |
| **同类型分配器可比较相等** | 见下，最容易被漏掉 |

```cpp
template <typename T, typename Config = DefaultConfig>
class Allocator {
public:
  using value_type = T;

  template <typename U>
  struct rebind { using other = Allocator<U, Config>; };

  T *allocate(std::size_t n);
  void deallocate(T *p, std::size_t n) noexcept;
};
```

## 3. 坑一：分配器的「相等」语义

标准规定：

> 若两个分配器相等，则**一个分配的存储可以由另一个释放**。

容器依赖这条做优化——当容器带着分配器移动/交换时，若分配器相等，可以直接
窃取缓冲区；若不等，就必须逐元素搬移。

本实现中所有实例共用同一个池，因此恒相等：

```cpp
template <typename U>
bool operator==(const Allocator<U, Config> &) const noexcept { return true; }
```

**如果漏掉这条会怎样**：`operator==` 缺失时 C++20 仍能编译（有默认推导），
但默认推导会退化成比较对象地址，**导致两个本该相等的分配器被判为不等**，
容器于是放弃缓冲区窃取优化、退化为逐元素搬移。功能正确但性能变差——
属于"不报错的性能 bug"。

**验证方式**（本项目测试里有）：

```cpp
Allocator<int> a, b;
MEMPOOL_EXPECT(a == b);
int *p = a.allocate(4);
b.deallocate(p, 4);      // 一个实例分配、另一个释放 —— 必须成立
```

## 4. 坑二：超对齐类型的布局冲突

这是本实现遇到的**真实设计冲突**，值得展开讲。

### 冲突从哪来

池的头部紧邻用户指针前方，而释放时靠 `p - kHeaderBytes` 找回头部：

```
   p - kHeaderBytes      p
        ↓                ↓
   ┌─────────┬────────────────────┐
   │  头部   │   用户数据         │
   └─────────┴────────────────────┘
```

这条"回头部"的路径成立的前提是：**头部尺寸是请求对齐的整数倍**。
常规情况（对齐 ≤ 16）满足，因为头部本身就是 16 字节。

但若类型要求 `alignas(64)`，就有问题了：

```
  align=64 时，p 必须 64 对齐；
  而 p - 16 与 p - 32 都不是 64 的倍数 ——
  从 p 减去固定的 16 字节，落不到头部起点上。
```

**在分配器这一层手工偏移指针是行不通的**：`deallocate(p, n)` 只拿到对齐后的
`p`，偏移量丢失了，无法还原真实基址。

### 本实现的解法：超对齐回落到系统分配

```cpp
if (align > kHeaderBytes) {
  return AllocateOverAligned(size, align);   // 走系统分配
}
```

超对齐路径用一个**前缀**记录真实基址与偏移：

```
   raw（真实基址）
    ↓
   ┌──────────┬────────┬──────────────────┐
   │ 前缀(16B)│  填充  │  用户数据(p)     │
   │ magic    │        │                  │
   │ offset   │        │                  │
   └──────────┴────────┴──────────────────┘
```

释放时从前缀读出 `offset`，即可还原 `raw`：

```cpp
const std::uintptr_t raw = addr - prefix->offset;
std::free(reinterpret_cast<void *>(raw));
```

**为什么用"前缀 + 偏移"而不是"固定偏移"**：因为对齐要求可能比 16 大得多，
固定偏移无法保证 `p` 满足对齐。记录偏移量让两条信息（真实基址、对齐位置）
都能在释放时还原。

### 代价

| 方面 | 代价 |
| --- | --- |
| 内存 | 最多多消耗 `align` 字节（用于对齐取整） |
| 速度 | 超对齐路径**不走池**，直接 `malloc`/`free`，失去池的复用收益 |
| 复杂度 | 多一条代码路径与一个前缀结构 |

**为什么接受**：超对齐类型（`alignas(64)` 的 SIMD 向量等）在实际代码里
占比很小，而且它们通常尺寸较大、数量较少——本来也不是池的目标场景。
**为少数情况牺牲常规路径的简洁性是错的**，所以这里选择"回落"而不是"统一处理"。

## 5. 用法示例

```cpp
#include "mempool_lab/allocator.hpp"

// 直接用于容器
std::vector<int, mempool_lab::Allocator<int>> v;
std::list<std::string, mempool_lab::Allocator<std::string>> l;
std::map<int, int, std::less<int>,
         mempool_lab::Allocator<std::pair<const int, int>>> m;

// 超对齐类型
struct alignas(64) Vec4 { float v[4]; };
std::vector<Vec4, mempool_lab::Allocator<Vec4>> simd;   // 仍满足 64 字节对齐
```

`std::list` / `std::map` 会自动 `rebind` 到节点类型，无需手动处理。

## 6. 一个观察：`deallocate` 刻意忽略 `n`

```cpp
void deallocate(T *p, std::size_t /*n*/) noexcept {
  Pool().Deallocate(p);      // 不使用 n
}
```

因为**尺寸由块头部记录**。这带来一个实际好处：

> 即便容器传入了一个错误的 `n`，也不会影响正确性。

对比"要求调用方传尺寸"的实现——那里 `n` 用错就会写坏空闲链表。
这是 [自描述头部](01-principles.md#3-为什么要自描述头部) 在容器场景下的直接收益。

## 7. 测试覆盖

`test/test_allocator.cpp` 共 9 个用例：

| 用例 | 覆盖点 |
| --- | --- |
| `vector_basic` | 基本读写 + 扩容 |
| `list_rebind` | **rebind 到节点类型** |
| `map_nodes` | 键值对节点分配 |
| `equality_semantics` | 相等性 + **交叉释放** |
| `zero_allocation` | `allocate(0)` 返回 nullptr |
| `overflow_guard` | `n * sizeof(T)` 回绕时抛 `bad_alloc` |
| `over_aligned_types` | 64/128 字节对齐类型 |
| `over_aligned_container` | `vector<alignas(64)>` 含扩容 |
| `pool_audit_after_container_use` | 大量容器使用后账目自洽 |

---

上一篇：[05 · 验证方法](05-verification.md) ·
[返回总览](00-overview.md)
