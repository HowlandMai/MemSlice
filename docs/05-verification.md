# 05 · 验证方法：如何证明一个池是对的

本文是项目的**差异化部分**。多数内存池实现只有功能测试，而"能跑通几个用例"
离"可以相信"还很远。下面是我们用的五层验证，以及**每层能抓到什么、抓不到什么**。

## 为什么需要分层验证

不同缺陷需要不同的手段才能暴露：

| 缺陷类型 | 单元测试 | ASan | TSan | 随机长跑 | 审计 |
| --- | --- | --- | --- | --- | --- |
| 功能写错（返回错尺寸） | ✓ | | | ✓ | |
| 越界读写 | | ✓ | | ✓ | |
| 内存泄漏 | | ✓ | | | |
| 数据竞争 | | | ✓ | | |
| **链表账目错乱** | | | | 部分 | **✓** |
| **同一块被交出两次** | | | | **✓** | ✓ |
| 同一块出现在两个位置 | | | | | **✓** |

**注意最后两行**：这类"不变量被破坏"的问题，ASan/TSan **都抓不到**——
因为它们不是越界访问、也不是竞争，而是**逻辑上不该发生却发生了的状态**。
这正是我们额外投入审计与随机长跑的原因。

## 第 1 层：功能测试

最基础的一层，验证"给定输入，行为是否符合预期"。

```cpp
MEMSLICE_CASE(basic, memory_is_reused) {
  MemoryPool<> pool;
  void *a = pool.Allocate(64);
  pool.Deallocate(a);
  void *b = pool.Allocate(64);
  MEMSLICE_EXPECT(a == b);   // 释放后同尺寸应复用同一地址（LIFO 链表）
}
```

覆盖：分配/释放、地址复用、零字节、边界尺寸、用户数据完整性、对齐、
大量小对象、超大请求、超范围请求。

**局限性**：只能验证"想到的情况"。没想到的组合测不到——需要下一层。

## 第 2 层：随机长跑（fuzz-lite）

用伪随机序列长时间跑 `alloc`/`free`/`realloc` 的组合，每步校验不变量。

**关键做法：固定种子。** 这是随机测试能被信任的前提——失败必须可复现。

```cpp
struct Rng {                    // xorshift32：确定性，跨平台一致
  std::uint32_t s;
  std::uint32_t Next() { ... }
};

MEMSLICE_CASE(fuzz, randomized_long_run) {
  Rng rng(20260920);            // ← 固定种子，失败可复现
  for (int round = 0; round < 3000; ++round) {
    // 随机选：分配 / 释放 / 重分配
    ...
    // 每轮校验三件事
    MEMSLICE_EXPECT(pool.Audit().ok);          // ① 池内部账目
    for (const auto &b: live) MEMSLICE_EXPECT(Intact(b));   // ② 用户数据未被破坏
    MEMSLICE_EXPECT(Disjoint(live));           // ③ 无重叠的存活块
  }
}
```

**每轮校验的三件事分别抓什么**：

1. `Audit()` —— 池自己认为账目对不对
2. `Intact()` —— 用户写进去的数据有没有被别人的分配覆盖（能抓"块被重复交出"）
3. `Disjoint()` —— 所有存活块的地址区间两两不重叠（抓"同一块给两个调用方"）

**局限性**：依赖随机序列覆盖到问题路径。3000 轮不是保证，是概率。

## 第 3 层：不变量审计

本项目最独特的一层。核心思路：**让池能自己判断"我的账目对不对"**。

```cpp
const auto report = pool.Audit();
if (!report.ok) {
  for (const char *v : report.violations) {
    std::fprintf(stderr, "池账目异常：%s\n", v);
  }
}
```

核对的不变量：

| 不变量 | 为什么重要 |
| --- | --- |
| 链表无环 | 环会让分配/遍历无限循环 |
| 节点按 `kAlignSize` 对齐 | 错位地址意味着链表被破坏 |
| 节点属于本池的 chunk | 排除外部指针混入 |
| 同一块不出现两次 | **直接对应"块被重复交出"这类严重缺陷** |
| 链表长度有上限 | 防止被污染的链表让审计自身失控 |

### 审计的关键设计：只查"真正可验证"的

写这一层时踩过坑：最初写了两条**错误的**检查，导致对正常负载报出 37 条误报。

| 写错的检查 | 为什么错 |
| --- | --- |
| 要求空闲节点 16 字节对齐 | 池按 8 字节切块，只保证 8 对齐 |
| 要求链表相邻节点地址差 == 桶容量 | 链表相邻性**不要求地址等差** |

**误报比没有检查更糟**——它会把真正的问题淹没在噪声里。

还有一条更隐蔽的：**审计自身必须不会挂死**。最初的实现先遍历链表、后查环，
遇到环状链表直接死循环。修法是**先查环**（Floyd 快慢指针）+ 长度安全阀。

### 审计必须被验证

一个"永远返回 ok"的审计也能通过"正常时不误报"的测试。所以必须**成对测试**：

```cpp
// 防误报：正常负载必须通过
MEMSLICE_CASE(audit, healthy_interleaved) { ...; MEMSLICE_EXPECT(pool.Audit().ok); }

// 防漏报：注入错误必须被抓到
MEMSLICE_CASE(audit, detects_cycle) {
  n0->next = n1; n1->next = n0;                  // 人为造一个环
  MEMSLICE_EXPECT(detail::InspectChain(n0, 8, 1000).cyclic);
}
MEMSLICE_CASE(audit, self_loop_terminates) {      // 防挂死
  n0->next = n0;
  const auto info = detail::InspectChain(n0, 8, 10);
  MEMSLICE_EXPECT(info.cyclic);
  MEMSLICE_EXPECT(info.length == 0);
}
```

**没有"防漏报"测试的审计，等于没有审计。**

## 第 4 层：Sanitizer

| 工具 | 抓什么 | 抓不到什么 |
| --- | --- | --- |
| **ASan** | 越界读写、use-after-free、double-free | 逻辑错误、竞态 |
| **UBSan** | 有符号溢出、空指针解引用、非法移位 | 内存布局错误 |
| **TSan** | 数据竞争、锁顺序问题 | 非并发类逻辑错误 |
| **valgrind** | 内存泄漏、未初始化读取 | 同上 |

用法：

```zsh
# ASan + UBSan
clang++ -std=c++20 -O1 -g -fsanitize=address,undefined \
        -I include -I test test/*.cpp src/*.cpp -o /tmp/asan -pthread
ASAN_OPTIONS=detect_leaks=0 /tmp/asan --quiet

# TSan
clang++ -std=c++20 -O1 -g -fsanitize=thread \
        -I include -I test test/*.cpp src/*.cpp -o /tmp/tsan -pthread
TSAN_OPTIONS=halt_on_error=1 /tmp/tsan --quiet

# valgrind
valgrind --leak-check=full ./build/linux/x86_64/release/test_memslice --quiet
```

**重要认知**：Sanitizer 全绿**不等于**没有 bug。它们只能发现"内存/并发层面
可观测的错误"，而账目错乱、逻辑不变量被破坏，它们看不到——那需要第 3 层。

## 第 5 层：并发专项

并发缺陷难以复现，需要针对性的用例设计：

| 用例 | 设计意图 |
| --- | --- |
| `shared_pool_stress` | 多线程共享池，覆盖锁的基本正确性 |
| `cross_thread_return` | **A 分配、B 释放**——所有权跨线程转移是最易错的方向 |
| `thread_lifecycle_churn` | 线程反复创建销毁，检验无累积泄漏/错乱 |
| `concurrent_pointer_uniqueness` | 全部存活时校验地址唯一（抓"同一块给两个线程"） |

### 一个测试设计的坑

`concurrent_pointer_uniqueness` 最初写成"边分配边释放边记录"，结果误报重叠。
原因：同一地址可能先以 `size=8` 被记录、释放后被另一线程以 `size=96` 复用再记录，
两条记录区间重叠。

**这是测试设计缺陷，不是池的缺陷。** 修法：**所有块必须同时存活**时才校验唯一性。

```cpp
// 阶段 1：全部线程只分配、保留（不释放）
// 阶段 2：join 后校验地址唯一 —— 此刻全部存活，才是有效的前提
// 阶段 3：统一释放
```

**教训**：写并发测试时必须想清"校验的前提条件"。前提不成立时的失败是噪声。

## 第 6 层：全量门禁

一条命令跑齐所有层：

```zsh
zsh scripts/ci.sh
```

覆盖：构建、用例、`xmake test` 门禁、零告警、clang-format、ASan+UBSan、
TSan、valgrind。任一失败即整体失败（退出码非 0）。

**脚本设计的两条纪律**：

1. **绝不用 `| tail` 之类的管道判断成败**——管道会吞掉真实退出码
   （这在实践中真的导致过"误报通过"）
2. **用例数从输出里数，不硬编码**——否则断言会与实现脱节

## 总结：验证的层次感

```
  单元测试        ← 能想到的情况
      ↓
  随机长跑        ← 想不到的组合
      ↓
  不变量审计      ← 逻辑上不该发生的状态 ← 本项目的独特之处
      ↓
  Sanitizer      ← 内存/并发层面的错误
      ↓
  并发专项        ← 难以复现的时序问题
      ↓
  全量门禁        ← 固化以上全部，防止回归
```

**核心观点**：一个"能自己抓错"的池，比一个"看起来能跑"的池可信得多。
验证不是开发完成后的补充，而是**设计的一部分**——不变量应该先写清楚，再写实现。

---

## 一条贯穿全文的方法论

> **先建立检查手段，再修改代码。**

这一条来自真实的失败经历：我在一个更早的实现上连续四轮"看症状→猜原因→改一版→
又崩"，每一轮只是把症状换成另一个，因为**没有手段验证假设**。

本文提到的所有定位方法都不复杂——一次 `fprintf`、一个 `static_assert`、
一次受控实验、一个注入错误的用例——但它们把"猜"变成了"看"。

详见图 [03 · 构建日志](03-building-log.md)。
