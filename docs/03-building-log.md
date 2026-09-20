# 03 · 构建日志：真实缺陷与修法

这一篇记录开发本实现过程中**实际踩到的缺陷**——包括症状、定位方法、根因与修法。

它比"最终代码"更有价值，因为**缺陷的定位过程**才是工程能力所在。所有缺陷都在
本项目里真实发生过（前三个来自一个更早的、更复杂的实现，教训被带到了这里）。

---

## 1. `count` 必须初始化为期望值

**症状**：任何一次分配都段错误，且崩在写完头部之前。

**定位**：用 `gdb` 拿到调用栈，定位到 `Refill` 里的串链循环：

```cpp
for (std::size_t i = 1;; ++i) {
  if (i == count - 1) { current->next = nullptr; break; }
  current->next = ... + size;
  current = current->next;
}
```

在 `Refill` 里打印 `count`，看到 **`count = 0`**。

**根因**：`count` 是**入参兼出参**——传入"希望切几个块"，返回"实际切了几个"。
代码里写成了：

```cpp
std::size_t count = 0;        // ✗ 错：0 会被当作"我希望切 0 个"
chunk = ChunkAlloc(size, count);
```

于是 `ChunkAlloc` 里 `want = count * size = 0`，走进"当前 chunk 足够"的分支
直接返回，**`count` 保持 0**。回到 `Refill` 后 `count - 1` 下溢为 `SIZE_MAX`，
循环永不结束，沿内存一路写出去 → 段错误。

**修法**：初始化为期望值，并加前置条件断言兜底。

```cpp
std::size_t count = Config::kBatchSize;   // ✓ 传"我期望切 20 个"
chunk = ChunkAlloc(size, count);
assert(count >= 1 && "ChunkAlloc 必须至少返回一个块");
```

同时在 `ChunkAlloc` 入口加防御：

```cpp
assert(count >= 1 && "count 必须 >= 1（入参语义为期望块数）");
if (count == 0) { count = 1; }   // 兜底，避免下溢
```

**教训**：
- `size_t` 下溢是静默的——`0 - 1` 得到 `SIZE_MAX`，编译器和运行时都不报错
- 对"入参兼出参"的参数，初始化语义必须想清楚：**是"0 个"还是"默认值"**
- **用断言把契约钉住**，比事后调试便宜得多

---

## 2. 头部与链表指针重叠

**症状**：释放时读到垃圾容量值（实测 `887432`、`1044920` 这类），
随后断言失败或段错误。分配本身正常，只有"释放一个刚从链表取出的块"才触发。

**定位**：先确认布局事实：

```
用户指针 p        = 0x...050
块起始 raw        = 0x...030
头部地址 h        = 0x...030   ← h == raw！头部就在块起始处
p - raw           = 32 字节
```

**根因**：头部写在 `p - kHeaderBytes`（32 字节），而
`p = align_up(raw + kHeaderBytes, 16)`。当 `raw` 本身 16 对齐时
`p = raw + kHeaderBytes`，于是 **`h == raw`**。

而空闲链表的 `next` 也写在 `raw` 处（前 8 字节）。**两者重叠。**

在简单实现里这没问题：块处于链表态时没有有效头部，头部只在分配后才写入，
两个状态天然分离。

但一旦引入**中间缓存层**（曾尝试的线程本地缓存），同一条内存会在两态间反复
流转，就会出现在"把块挂回链表之后"仍读写头部的路径——读到的自然是链表指针。

**修法**（两条都采纳）：

1. **接口分层**：头部只在 `MemoryPool` 层读写；底层 `FreeListPool` 只处理裸块，
   完全不碰头部。这样"谁在什么时候碰头部"变得明确。
2. **顺序约束**：`Deallocate` 里**先把所需信息（capacity/source/base）全部取出**，
   再交给底层回收。回收之后立即返回，不再触碰头部：

```cpp
const std::size_t capacity = UnpackCapacity(h->packed);  // ① 先读
const BlockSource source   = UnpackSource(h->packed);
void *const base           = h->base;
if (source == BlockSource::kPool) {
  pool_.Deallocate(base, capacity);   // ② 后写（此处 next 覆盖头部）
}                                     // ③ 之后不得再碰头部
```

**教训**：
- **内存布局的重叠点必须显式记录**，否则它会在引入新机制时突然变成 bug
- "先读出所需信息、再破坏性操作"是这类问题的通用解法
- 分层不只是为了整洁——**它让"哪一层可以碰什么"成为可验证的约束**

---

## 3. 头部尺寸必须是 `max_align_t` 的整数倍

**症状**：把头部从 16 字节改成 24 字节（为了加一个字段）后，立刻段错误。
崩溃地址解出来是 ASCII 字符串内容（如 `"ate"`，来自 `"allocate/..."` 之类的
字符串）——说明**空闲链表里存进了用户数据**。

**定位**：受控实验——只改取整基准，其他都不动：

```
kHeaderSize = 24（按 alignof(Header)=8 取整）  → 段错误
kHeaderSize = 32（按 alignof(max_align_t)=16 取整）→ 全部用例通过
```

**根因**：头部写在 `p - kHeaderBytes`，而用户指针
`p = align_up(raw + kHeaderBytes, align)`（`align ≥ max_align_t`）。

只有当 `kHeaderBytes` 是 `align` 的**整数倍**时，"头部相对 `base` 的偏移"才是
恒定的，分配时写入与回收时读取才算同一个地址。取 24 时偏移会在 0 与 8 之间
摆动，`HeaderOf(p)` 读到的是**错位的内存**。

**修法**：把约束写成编译期断言，让它无法被误改。

```cpp
constexpr std::size_t kHeaderBytes = [] {
  constexpr std::size_t raw = sizeof(Header);
  constexpr std::size_t align = alignof(std::max_align_t);
  return ((raw + align - 1) / align) * align;   // 取整到 16 的倍数
}();

static_assert(kHeaderBytes % alignof(std::max_align_t) == 0,
              "头部字节数必须是 max_align_t 的整数倍（否则头部与用户指针会错位）");
```

**教训**：
- 有些约束"看起来是保守偏好"，实际是**硬性正确性要求**——把它写成 `static_assert`，
  而不是留一句注释
- 定位这类问题，**受控实验**（只改一个变量）比读代码快得多
- 崩溃地址是**可解读的信息**：把十六进制解成字节，可能直接告诉你"这是什么数据"

---

## 4. 审计自身也可能有 bug

这是"验证工具也需要被验证"的案例。写不变量审计时，我先写了**两条错误的检查**：

| 错误的检查 | 为什么错 |
| --- | --- |
| 要求空闲节点 16 字节对齐 | 池按 `kAlignSize`(8) 切块，只保证 8 对齐。`Header` 的 `alignas` 约束的是类型别名，不是池内块的实际地址 |
| 要求链表相邻节点地址差 == 桶容量 | 空闲链表的相邻性只表示"都是空闲块"，**不要求地址等差**。同桶可混入不同来源的块 |

结果审计对**正常负载**报出 37 条"违规"——全是误报，把真正的问题淹没了。

**还有一个更隐蔽的问题**：审计的遍历顺序。最初的实现是**先遍历链表、后查环**，
遇到环状链表时直接**挂死**（无限循环）。

**修法**：

1. 只检查**真正可验证**的不变量
2. 遍历前**先做环检测**（Floyd 快慢指针），并带长度安全阀：

```cpp
// 先查环——顺序关键，否则有环时遍历会死循环
while (fast && fast->next) { slow = slow->next; fast = fast->next->next;
                             if (slow == fast) { cyclic = true; break; } }
if (cyclic) return;                       // 有环就不再遍历
for (n = head; n; n = n->next) {
  if (++length > max_nodes) { overlong = true; break; }   // 安全阀
}
```

3. **测试审计的两面性**：既测"正常时不误报"，也测"注入错误时必须报出"。
   只测前者的话，一个永远返回 `ok` 的审计也能通过。

```cpp
MEMPOOL_CASE(audit, detects_cycle) {         // 防漏报
  n0->next = n1; n1->next = n0;
  MEMPOOL_EXPECT(detail::InspectChain(n0, 8, 1000).cyclic);
}
MEMPOOL_CASE(audit, self_loop_terminates) {  // 防挂死
  n0->next = n0;
  const auto info = detail::InspectChain(n0, 8, 10);
  MEMPOOL_EXPECT(info.cyclic);
}
```

**教训**：
- **验证工具本身也是代码，同样会错**。错误的检查会制造误报，比没有检查更糟
- 工具的**健壮性**要求比被测代码更高——审计挂死比不审计更浪费时间
- 测试要成对写：**防误报 + 防漏报**，否则测试可能是安慰剂

---

## 5. 一条贯穿性的方法论

上面四个缺陷指向同一个结论：

> **先建立检查手段，再修改代码。**

顺序反了会发生什么，我在一个更早的实现上验证过：连续四轮"看到症状→猜原因→
改一版→又崩"，每一轮都只是把一个症状换成另一个，因为**没有手段验证假设**。

而本文档里的缺陷之所以能逐个定位，都是先有了手段：

- `count` 下溢 → 先在 `Refill` 打印实际值
- 头部重叠 → 先打印指针布局，确认 `h == raw`
- 对齐约束 → 先做受控实验，只改一个变量
- 审计误报 → 先用"注入错误"证明审计的能力边界

**这些手段都不复杂**（一次 `fprintf`、一次 `static_assert`、一个对比实验），
但它们把"猜"变成了"看"。

---

## 6. 缺陷清单（速查）

| # | 缺陷 | 根因 | 防线 |
| --- | --- | --- | --- |
| 1 | `count` 为 0 导致下溢 | 入参兼出参未初始化 | 断言 + 兜底 |
| 2 | 释放读到垃圾容量 | `next` 与头部重叠；归还后仍读头部 | 分层 + 顺序约束 |
| 3 | 改头部尺寸即段错误 | `kHeaderBytes` 非 `max_align_t` 整数倍 | `static_assert` |
| 4 | 审计误报 37 条 | 检查了不成立的不变量 | 只查可验证项 + 成对测试 |
| 5 | 审计遇环挂死 | 先遍历后查环 | 先查环 + 长度安全阀 |

---

下一篇：[04 · 性能分析](04-performance-notes.md) —— 数据与归因。
再做一篇：[05 · 验证方法](05-verification.md) —— 如何证明一个池是对的。
