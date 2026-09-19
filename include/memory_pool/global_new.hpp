// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_GLOBAL_NEW_HPP_
#define MEMORY_POOL_MEMORY_POOL_GLOBAL_NEW_HPP_

#include <cstddef>
#include <new>

// 重载全局 new/delete 以使用内存池（声明部分）。
// 实现位于 src/global_new.cpp——只有链接该文件（xmake 目标 memory_pool_global）
// 才会让本组重载生效；仅包含本头文件而不链接实现不会产生任何副作用。
// 说明：所有从全局 new/delete 分配的内存，都在用户指针前放置自描述头部，
// 记录真实底层基址，以及打包了块容量/来源/分配状态/魔数的机器字，
// 这样所有 delete 形态（含无参版本）都能正确回收，尺寸参数一律忽略。
// 头部布局见 block.hpp 的 detail::RawHeader。
void *operator new(size_t size);
void *operator new(size_t size, std::align_val_t alignment);
void operator delete(void *p) noexcept;
void operator delete(void *p, size_t size) noexcept;
void operator delete(void *p, std::align_val_t alignment) noexcept;
void operator delete(void *p, size_t size, std::align_val_t alignment) noexcept;
void *operator new[](size_t size);
void *operator new[](size_t size, std::align_val_t alignment);
void operator delete[](void *p) noexcept;
void operator delete[](void *p, size_t size) noexcept;
void operator delete[](void *p, std::align_val_t alignment) noexcept;
void operator delete[](void *p, size_t size, std::align_val_t alignment) noexcept;

#endif // MEMORY_POOL_MEMORY_POOL_GLOBAL_NEW_HPP_
