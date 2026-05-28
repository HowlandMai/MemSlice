// Copyright 2025 MemoryPool Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "../include/memory_pool.hpp"
#include <cstdlib>
#include <cstring>

namespace memory_pool {

// 全局内存池实例
// Global memory pool instance
MemoryPool<> default_memory_pool;

}  // namespace memory_pool

// 重载全局operator new以使用内存池
// Overload global operator new to use memory pool
void* operator new(size_t size) {
  return memory_pool::default_memory_pool.Allocate(size);
}

// 重载全局operator delete以使用内存池
// Overload global operator delete to use memory pool
void operator delete(void* p, size_t size) noexcept {
  if (p != nullptr) {
    memory_pool::default_memory_pool.Deallocate(p, size);
  }
}

// 重载全局operator delete（无大小参数版本）
// Overload global operator delete (no size parameter version)
void operator delete(void* p) noexcept {
  if (p != nullptr) {
    // 注意：在C++14及以上，编译器会优先调用带size参数的版本
    // 此版本仅作为后备
    // Note: In C++14 and above, the compiler will prefer the version with size parameter
    // This version is only as a fallback
    memory_pool::default_memory_pool.Deallocate(p, sizeof(void*));
  }
}

// 重载全局operator new[]以使用内存池
// Overload global operator new[] to use memory pool
void* operator new[](size_t size) {
  return memory_pool::default_memory_pool.Allocate(size);
}

// 重载全局operator delete[]以使用内存池
// Overload global operator delete[] to use memory pool
void operator delete[](void* p, size_t size) noexcept {
  if (p != nullptr) {
    memory_pool::default_memory_pool.Deallocate(p, size);
  }
}

// 重载全局operator delete[]（无大小参数版本）
// Overload global operator delete[] (no size parameter version)
void operator delete[](void* p) noexcept {
  if (p != nullptr) {
    // 注意：在C++14及以上，编译器会优先调用带size参数的版本
    // 此版本仅作为后备
    // Note: In C++14 and above, the compiler will prefer the version with size parameter
    // This version is only as a fallback
    memory_pool::default_memory_pool.Deallocate(p, sizeof(void*));
  }
}

