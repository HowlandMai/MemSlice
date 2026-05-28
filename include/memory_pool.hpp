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

#ifndef MEMORY_POOL_MEMORY_POOL_HPP_
#define MEMORY_POOL_MEMORY_POOL_HPP_

#include <cstddef>
#include <new>
#include <limits>
#include <type_traits>
#include <cstdlib>
#include <cstring>

namespace memory_pool {

// 内存池配置参数模板
// Memory pool configuration template
struct DefaultConfig {
  static constexpr size_t kMaxSmallObjectBytes = 128;  // 二级分配器管理的最大字节数
  static constexpr size_t kAlignSize = 8;              // 内存对齐大小
  static constexpr size_t kChunkSize = 1024;           // 每次向系统申请的内存块大小
  static constexpr int kDefaultNobjs = 20;             // 默认一次分配的对象数量
};

// 内存块结构
// Memory block structure
union alignas(std::max_align_t) MemBlock {
  MemBlock* next;  // 指向下一个空闲内存块
  char data[1];    // 内存块数据区域
};

// 一级分配器（直接向系统申请内存）
// First-level allocator (directly allocate from system)
template <typename Config>
class FirstLevelAllocator {
 public:
  // 分配内存
  // Allocate memory
  [[nodiscard]] static void* Allocate(size_t n) noexcept {
    return std::malloc(n);
  }

  // 释放内存
  // Deallocate memory
  static void Deallocate(void* p, size_t /*n*/) noexcept {
    std::free(p);
  }

  // 重新分配内存
  // Reallocate memory
  [[nodiscard]] static void* Reallocate(void* p, size_t old_size, size_t new_size) noexcept {
    return std::realloc(p, new_size);
  }
};

// 二级分配器（管理小内存块）
// Second-level allocator (manage small memory blocks)
template <typename Config>
class SecondLevelAllocator {
 private:
  // 计算空闲链表数量
  // Calculate number of free lists
  static constexpr size_t kNumFreeLists = Config::kMaxSmallObjectBytes / Config::kAlignSize;

  // 获取内存对齐后的大小
  // Get aligned size
  [[nodiscard]] static constexpr size_t RoundUp(size_t bytes) noexcept {
    return (bytes + Config::kAlignSize - 1) & ~(Config::kAlignSize - 1);
  }

  // 获取对应的空闲链表索引
  // Get free list index
  [[nodiscard]] static constexpr size_t FreeListIndex(size_t bytes) noexcept {
    return (RoundUp(bytes) - 1) / Config::kAlignSize;
  }

  // 空闲链表数组
  // Free list array
  MemBlock* free_list_[kNumFreeLists]{};

  // 指向当前内存块的起始位置
  // Pointer to current memory chunk start
  char* start_free_ = nullptr;

  // 指向当前内存块的结束位置
  // Pointer to current memory chunk end
  char* end_free_ = nullptr;

  // 内存池已分配的总内存大小
  // Total memory allocated by memory pool
  size_t heap_size_ = 0;

 public:
  // 构造函数
  // Constructor
  constexpr SecondLevelAllocator() noexcept = default;

  // 禁止拷贝构造和赋值操作
  // Prohibit copy constructor and assignment
  SecondLevelAllocator(const SecondLevelAllocator&) = delete;
  SecondLevelAllocator& operator=(const SecondLevelAllocator&) = delete;

  // 分配内存
  // Allocate memory
  [[nodiscard]] void* Allocate(size_t n) {
    // 内存对齐
    // Memory alignment
    size_t aligned_size = RoundUp(n);

    // 从对应的空闲链表中获取内存
    // Get memory from corresponding free list
    MemBlock** my_free_list = free_list_ + FreeListIndex(aligned_size);
    MemBlock* result = *my_free_list;

    if (result == nullptr) {
      // 如果空闲链表为空，重新填充
      // If free list is empty, refill it
      return Refill(aligned_size);
    }

    // 从空闲链表中取出第一个内存块
    // Take the first memory block from free list
    *my_free_list = result->next;
    return result;
  }

  // 释放内存
  // Deallocate memory
  void Deallocate(void* p, size_t n) noexcept {
    // 内存对齐
    // Memory alignment
    size_t aligned_size = RoundUp(n);

    // 将内存块添加到对应的空闲链表
    // Add memory block to corresponding free list
    MemBlock** my_free_list = free_list_ + FreeListIndex(aligned_size);
    MemBlock* q = reinterpret_cast<MemBlock*>(p);
    q->next = *my_free_list;
    *my_free_list = q;
  }

  // 重新分配内存
  // Reallocate memory
  [[nodiscard]] void* Reallocate(void* p, size_t old_size, size_t new_size) {
    // 释放旧内存，分配新内存
    // Free old memory and allocate new memory
    void* result = Allocate(new_size);
    if (result != nullptr) {
      // 拷贝旧内存中的数据到新内存
      // Copy data from old memory to new memory
      size_t copy_size = (old_size < new_size) ? old_size : new_size;
      std::memcpy(result, p, copy_size);
      Deallocate(p, old_size);
    }
    return result;
  }

 private:
  // 向系统申请内存并分配给空闲链表
  // Second-level allocator: Allocate chunk from system and distribute to free list
  [[nodiscard]] void* Refill(size_t n) {
    int nobjs = Config::kDefaultNobjs;  // 默认申请的对象数量

    // 向系统申请大块内存
    // Allocate large chunk from system
    char* chunk = ChunkAlloc(n, nobjs);

    // 如果只申请到一个对象，直接返回
    // If only one object is allocated, return it directly
    if (nobjs == 1) {
      return chunk;
    }

    // 否则，将剩余的对象添加到对应的空闲链表
    // Otherwise, add remaining objects to corresponding free list
    MemBlock** my_free_list = free_list_ + FreeListIndex(n);
    MemBlock* result = reinterpret_cast<MemBlock*>(chunk);
    *my_free_list = reinterpret_cast<MemBlock*>(chunk + n);
    MemBlock* current = *my_free_list;

    // 链接所有内存块
    // Link all memory blocks
    for (int i = 1; ; ++i) {
      MemBlock* next = reinterpret_cast<MemBlock*>(reinterpret_cast<char*>(current) + n);
      if (i == nobjs - 1) {
        current->next = nullptr;
        break;
      }
      current->next = next;
      current = next;
    }

    return result;
  }

  // 向系统申请大块内存
  // Second-level allocator: Allocate large chunk from system
  [[nodiscard]] char* ChunkAlloc(size_t size, int& nobjs) {
    char* result;
    size_t total_bytes = size * nobjs;
    size_t bytes_left = end_free_ - start_free_;

    // 如果当前内存块有足够的空间
    // If current memory chunk has enough space
    if (bytes_left >= total_bytes) {
      result = start_free_;
      start_free_ += total_bytes;
      return result;
    } else if (bytes_left >= size) {
      // 如果当前内存块空间不够，但至少能容纳一个对象
      // If current memory chunk has insufficient space, but can hold at least one object
      nobjs = static_cast<int>(bytes_left / size);
      total_bytes = size * nobjs;
      result = start_free_;
      start_free_ += total_bytes;
      return result;
    } else {
      // 如果当前内存块空间连一个对象都容纳不下
      // If current memory chunk cannot hold even one object
      size_t bytes_to_get = 2 * total_bytes + RoundUp(heap_size_ >> 4);

      // 尝试将当前内存块剩余的空间分配给对应的空闲链表
      // Try to allocate remaining space of current chunk to corresponding free list
      if (bytes_left > 0) {
        MemBlock** my_free_list = free_list_ + FreeListIndex(bytes_left);
        reinterpret_cast<MemBlock*>(start_free_)->next = *my_free_list;
        *my_free_list = reinterpret_cast<MemBlock*>(start_free_);
      }

      // 向系统申请新的内存块
      // Allocate new memory chunk from system
      start_free_ = reinterpret_cast<char*>(std::malloc(bytes_to_get));
      if (start_free_ == nullptr) {
        // 如果系统内存分配失败，尝试从空闲链表中寻找可用内存
        // If system memory allocation fails, try to find available memory from free lists
        for (size_t i = size; i <= Config::kMaxSmallObjectBytes; i += Config::kAlignSize) {
          MemBlock** my_free_list = free_list_ + FreeListIndex(i);
          MemBlock* p = *my_free_list;
          if (p != nullptr) {
            // 从空闲链表中获取内存块
            // Get memory block from free list
            *my_free_list = p->next;
            start_free_ = reinterpret_cast<char*>(p);
            end_free_ = start_free_ + i;
            return ChunkAlloc(size, nobjs);  // 递归调用重新分配
          }
        }

        // 如果所有空闲链表都没有可用内存，抛出异常
        // If no available memory in all free lists, throw exception
        throw std::bad_alloc();
      }

      // 更新内存池状态
      // Update memory pool status
      heap_size_ += bytes_to_get;
      end_free_ = start_free_ + bytes_to_get;

      // 递归调用重新分配
      // Recursive call to reallocate
      return ChunkAlloc(size, nobjs);
    }
  };
};

// 内存池主类（根据内存大小自动选择一级或二级分配器）
// Memory pool main class (automatically select allocator based on size)
template <typename Config = DefaultConfig>
class MemoryPool {
 private:
  FirstLevelAllocator<Config> first_level_alloc_;
  SecondLevelAllocator<Config> second_level_alloc_;

 public:
  // 构造函数
  // Constructor
  constexpr MemoryPool() noexcept = default;

  // 禁止拷贝构造和赋值操作
  // Prohibit copy constructor and assignment
  MemoryPool(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;

  // 分配内存
  // Allocate memory
  [[nodiscard]] void* Allocate(size_t n) {
    if (n > Config::kMaxSmallObjectBytes) {
      // 大内存块使用一级分配器
      // Large memory blocks use first-level allocator
      void* result = first_level_alloc_.Allocate(n);
      if (result == nullptr) {
        throw std::bad_alloc();
      }
      return result;
    } else {
      // 小内存块使用二级分配器
      // Small memory blocks use second-level allocator
      return second_level_alloc_.Allocate(n);
    }
  }

  // 释放内存
  // Deallocate memory
  void Deallocate(void* p, size_t n) noexcept {
    if (p == nullptr) {
      return;
    }

    if (n > Config::kMaxSmallObjectBytes) {
      // 大内存块使用一级分配器释放
      // Large memory blocks use first-level allocator to deallocate
      first_level_alloc_.Deallocate(p, n);
    } else {
      // 小内存块使用二级分配器释放
      // Small memory blocks use second-level allocator to deallocate
      second_level_alloc_.Deallocate(p, n);
    }
  }

  // 重新分配内存
  // Reallocate memory
  [[nodiscard]] void* Reallocate(void* p, size_t old_size, size_t new_size) {
    if (p == nullptr) {
      return Allocate(new_size);
    }

    if (old_size > Config::kMaxSmallObjectBytes && new_size > Config::kMaxSmallObjectBytes) {
      // 新旧内存块都大于阈值，使用一级分配器重新分配
      // Both old and new blocks are larger than threshold, use first-level allocator
      void* result = first_level_alloc_.Reallocate(p, old_size, new_size);
      if (result == nullptr) {
        throw std::bad_alloc();
      }
      return result;
    } else if (new_size <= Config::kMaxSmallObjectBytes) {
      // 新内存块小于等于阈值，使用二级分配器
      // New block is smaller than or equal to threshold, use second-level allocator
      void* new_p = Allocate(new_size);
      size_t copy_size = (old_size < new_size) ? old_size : new_size;
      std::memcpy(new_p, p, copy_size);
      Deallocate(p, old_size);
      return new_p;
    } else {
      // 旧内存块小于阈值，新内存块大于阈值，分别使用不同分配器
      // Old block is smaller than threshold, new block is larger, use different allocators
      void* new_p = Allocate(new_size);
      std::memcpy(new_p, p, old_size);
      Deallocate(p, old_size);
      return new_p;
    }
  }
};

// 全局内存池实例
// Global memory pool instance
extern MemoryPool<> default_memory_pool;

// 内存分配器类型（符合STL分配器要求）
// Memory allocator type (meets STL allocator requirements)
template <typename T, typename Config = DefaultConfig>
class Allocator {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using propagate_on_container_move_assignment = std::true_type;

  // 构造函数
  // Constructor
  constexpr Allocator() noexcept = default;

  // 拷贝构造函数
  // Copy constructor
  template <typename U>
  constexpr Allocator(const Allocator<U, Config>&) noexcept {}

  // 分配内存
  // Allocate memory
  [[nodiscard]] T* allocate(size_type n) {
    if (n > std::numeric_limits<size_type>::max() / sizeof(T)) {
      throw std::bad_alloc();
    }
    return static_cast<T*>(default_memory_pool.Allocate(n * sizeof(T)));
  }

  // 释放内存
  // Deallocate memory
  void deallocate(T* p, size_type n) noexcept {
    default_memory_pool.Deallocate(p, n * sizeof(T));
  }
};

// 分配器相等性比较
// Allocator equality comparison
template <typename T, typename U, typename Config>
constexpr bool operator==(const Allocator<T, Config>&, const Allocator<U, Config>&) noexcept {
  return true;
}

// 分配器不等性比较
// Allocator inequality comparison
template <typename T, typename U, typename Config>
constexpr bool operator!=(const Allocator<T, Config>&, const Allocator<U, Config>&) noexcept {
  return false;
}

}  // namespace memory_pool

// 重载全局operator new和operator delete以使用内存池
// Overload global operator new and operator delete to use memory pool
void* operator new(size_t size);
void operator delete(void* p) noexcept;
void* operator new[](size_t size);
void operator delete[](void* p) noexcept;

#endif  // MEMORY_POOL_MEMORY_POOL_HPP_

