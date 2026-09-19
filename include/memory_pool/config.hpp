// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_CONFIG_HPP_
#define MEMORY_POOL_MEMORY_POOL_CONFIG_HPP_

#include <cstddef>

namespace memory_pool {

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
  } // namespace detail

  // 内存池配置参数模板
  struct DefaultConfig {
    static constexpr size_t kMaxSmallObjectBytes = 128; // 二级分配器管理的最大字节数
    static constexpr size_t kAlignSize = 8; // 内存对齐大小（必须是 2 的幂）
    static constexpr int kDefaultNobjs = 20; // 默认一次分配的对象数量
    static constexpr bool kThreadSafe = true; // 二级分配器是否加锁（默认线程安全）
  };

} // namespace memory_pool

#endif // MEMORY_POOL_MEMORY_POOL_CONFIG_HPP_
