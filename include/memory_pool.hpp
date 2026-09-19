// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_HPP_
#define MEMORY_POOL_MEMORY_POOL_HPP_

// 伞头文件（umbrella header）：向后兼容入口，等价于包含全部子头文件。
//
// 按需包含时可直接引用具体子头，以缩小编译期依赖并避免不需要的语义：
//   #include "memory_pool/second_level.hpp"  仅二级分配器
//   #include "memory_pool/pool.hpp"         池门面 + 全局池声明
//   #include "memory_pool/allocator.hpp"    STL 适配器
//   #include "memory_pool/global_new.hpp"   全局 new/delete 重载声明
//                                           （需链接 memory_pool_global 才生效）

#include "memory_pool/allocator.hpp"
#include "memory_pool/block.hpp"
#include "memory_pool/config.hpp"
#include "memory_pool/first_level.hpp"
#include "memory_pool/global_new.hpp"
#include "memory_pool/pool.hpp"
#include "memory_pool/second_level.hpp"

#endif // MEMORY_POOL_MEMORY_POOL_HPP_
