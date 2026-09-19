// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_CONFIG_HPP_
#define MEMORY_POOL_MEMORY_POOL_CONFIG_HPP_

#include <cstddef>

namespace memory_pool {

  // ---------------------------------------------------------------------------
  // 调试期哨兵校验的默认开关
  //
  // 为什么不用 #ifdef NDEBUG 自动判断：构建系统未必定义 NDEBUG（例如 xmake 的
  // release 模式默认就不定义），依赖它会让「发布构建」静默保留校验代码、或反过来
  // 让调试构建悄悄丢掉诊断能力——两种都很难察觉。
  //
  // 因此改为显式、单一声明的开关：
  //   - 默认值由 MEMSLICE_DEBUG_CHECKS 宏决定（构建时可用 -DMEMSLICE_DEBUG_CHECKS=0/1 覆盖）；
  //   - 未定义该宏时默认**关闭**，即「零开销」是默认行为，需要诊断时显式打开；
  //   - 也可通过自定义 Config 的 kDebugChecks 逐池覆盖。
  //
  // 打开后的行为：释放前校验头部魔数与分配状态位，可捕获重复释放、野指针、
  // 头部被越界写坏等误用（详见 block.hpp / pool.hpp）。代价是每次释放多两次比较。
  // ---------------------------------------------------------------------------
#if defined(MEMSLICE_DEBUG_CHECKS)
  constexpr bool kDebugChecksDefault = (MEMSLICE_DEBUG_CHECKS != 0);
#else
  constexpr bool kDebugChecksDefault = false;
#endif

  // 配置工具：可选地读取配置中的线程安全开关，缺省时默认启用线程安全
  namespace detail {
    template<typename Config>
    inline constexpr bool kThreadSafeConfig = [] {
      if constexpr (requires { Config::kThreadSafe; }) {
        return Config::kThreadSafe;
      } else {
        return true;
      }
    }();

    // 调试校验开关：读取配置项 kDebugChecks，缺省回落到全局默认值。
    template<typename Config>
    inline constexpr bool kDebugChecksConfig = [] {
      if constexpr (requires { Config::kDebugChecks; }) {
        return Config::kDebugChecks;
      } else {
        return kDebugChecksDefault;
      }
    }();
  } // namespace detail

  // 内存池配置参数模板
  struct DefaultConfig {
    static constexpr size_t kMaxSmallObjectBytes = 128; // 二级分配器管理的最大字节数
    static constexpr size_t kAlignSize = 8; // 内存对齐大小（必须是 2 的幂）
    static constexpr int kDefaultNobjs = 20; // 默认一次分配的对象数量
    static constexpr bool kThreadSafe = true; // 二级分配器是否加锁（默认线程安全）
    static constexpr bool kDebugChecks = kDebugChecksDefault; // 释放前哨兵校验
  };

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_CONFIG_HPP_
