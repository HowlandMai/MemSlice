// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_MEMORY_POOL_HPP_
#define MEMORY_POOL_MEMORY_POOL_HPP_

#include <cstddef>
#include <new>
#include <limits>
#include <type_traits>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <mutex>

namespace memory_pool {

// 配置工具：可选地读取配置中的线程安全开关，缺省时默认启用线程安全
namespace detail {
template <typename Config>
inline constexpr bool kThreadSafeConfig = [] {
  if constexpr (requires { Config::kThreadSafe; }) {
    return Config::kThreadSafe;
  } else {
    return true;
  }
}();
}  // namespace detail

// 内存池配置参数模板
struct DefaultConfig {
  static constexpr size_t kMaxSmallObjectBytes = 128;  // 二级分配器管理的最大字节数
  static constexpr size_t kAlignSize = 8;              // 内存对齐大小（必须是 2 的幂）
  static constexpr size_t kChunkSize = 1024;           // 每次向系统申请的内存块大小（保留字段）
  static constexpr int kDefaultNobjs = 20;             // 默认一次分配的对象数量
  static constexpr bool kThreadSafe = true;            // 二级分配器是否加锁（默认线程安全）
};

// 内存块结构
union alignas(std::max_align_t) MemBlock {
  MemBlock* next;  // 指向下一个空闲内存块
  char data[1];    // 内存块数据区域
};

// 一级分配器（直接向系统申请内存）
template <typename Config>
class FirstLevelAllocator {
 public:
  // 分配内存
  [[nodiscard]] static void* Allocate(size_t n) noexcept {
    return std::malloc(n);
  }

  // 释放内存
  static void Deallocate(void* p, size_t /*n*/) noexcept {
    std::free(p);
  }

  // 重新分配内存
  [[nodiscard]] static void* Reallocate(void* p, size_t /*old_size*/, size_t new_size) noexcept {
    return std::realloc(p, new_size);
  }
};

// 二级分配器（管理小内存块）
template <typename Config>
class SecondLevelAllocator {
 private:
  // 配置校验：对齐大小必须为正、为 2 的幂，且最大小对象字节数必须为其整数倍
  static_assert(Config::kAlignSize > 0,
                "对齐大小必须为正数");
  static_assert((Config::kAlignSize & (Config::kAlignSize - 1)) == 0,
                "对齐大小必须是 2 的幂");
  static_assert(Config::kMaxSmallObjectBytes > 0,
                "最大小对象字节数必须为正数");
  static_assert(Config::kMaxSmallObjectBytes % Config::kAlignSize == 0,
                "最大小对象字节数必须是对齐大小的整数倍");
  static_assert(Config::kDefaultNobjs > 0,
                "默认申请对象数必须为正数");

  // 计算空闲链表数量
  static constexpr size_t kNumFreeLists = Config::kMaxSmallObjectBytes / Config::kAlignSize;

  // 获取内存对齐后的大小（对 0 字节按最小粒度处理，避免无符号下溢）
  [[nodiscard]] static constexpr size_t RoundUp(size_t bytes) noexcept {
    if (bytes == 0) {
      return Config::kAlignSize;  // 0 字节分配按最小粒度处理，避免 (0-1) 下溢
    }
    return (bytes + Config::kAlignSize - 1) & ~(Config::kAlignSize - 1);
  }

  // 获取对应的空闲链表索引
  [[nodiscard]] static constexpr size_t FreeListIndex(size_t bytes) noexcept {
    return (RoundUp(bytes) - 1) / Config::kAlignSize;
  }

  // 空闲链表数组
  MemBlock* free_list_[kNumFreeLists]{};

  // 指向当前内存块的起始位置
  char* start_free_ = nullptr;

  // 指向当前内存块的结束位置
  char* end_free_ = nullptr;

  // 内存池已分配的总内存大小
  size_t heap_size_ = 0;

  // 已申请内存块的基址链表（用于析构时回收）
  struct ChunkNode {
    char* base;
    size_t size;
    ChunkNode* next;
  };
  ChunkNode* chunk_list_ = nullptr;

  // 线程安全开关：为真时使用互斥锁，为假时使用空操作锁
  struct NoopLock {
    void lock() noexcept {}
    void unlock() noexcept {}
  };
  using MutexT = std::conditional_t<detail::kThreadSafeConfig<Config>, std::mutex, NoopLock>;
  using LockGuard = std::lock_guard<MutexT>;
  mutable MutexT mutex_;

 public:
  // 构造函数
  SecondLevelAllocator() = default;

  // 析构函数：回收所有向系统申请的内存块，避免内存泄漏
  ~SecondLevelAllocator() {
    ChunkNode* n = chunk_list_;
    while (n != nullptr) {
      std::free(n->base);
      ChunkNode* cur = n;
      n = n->next;
      std::free(cur);
    }
    chunk_list_ = nullptr;
  }

  // 禁止拷贝构造和赋值操作
  SecondLevelAllocator(const SecondLevelAllocator&) = delete;
  SecondLevelAllocator& operator=(const SecondLevelAllocator&) = delete;

  // 分配内存
  [[nodiscard]] void* Allocate(size_t n) {
    LockGuard guard(mutex_);
    return AllocateImpl(n);
  }

  // 释放内存
  void Deallocate(void* p, size_t n) noexcept {
    LockGuard guard(mutex_);
    DeallocateImpl(p, n);
  }

  // 重新分配内存
  [[nodiscard]] void* Reallocate(void* p, size_t old_size, size_t new_size) {
    LockGuard guard(mutex_);
    return ReallocateImpl(p, old_size, new_size);
  }

  // 当前池向系统申请的总字节数（用于诊断/测试）
  [[nodiscard]] size_t heap_size() const noexcept {
    return heap_size_;
  }

 private:
  // 分配内存（调用方已持有锁）
  [[nodiscard]] void* AllocateImpl(size_t n) {
    size_t aligned_size = RoundUp(n);
    assert(aligned_size <= Config::kMaxSmallObjectBytes);

    MemBlock** my_free_list = free_list_ + FreeListIndex(aligned_size);
    MemBlock* result = *my_free_list;

    if (result == nullptr) {
      return Refill(aligned_size);
    }

    *my_free_list = result->next;
    return result;
  }

  // 释放内存（调用方已持有锁）
  void DeallocateImpl(void* p, size_t n) noexcept {
    if (p == nullptr) {
      return;
    }

    size_t aligned_size = RoundUp(n);
    assert(aligned_size <= Config::kMaxSmallObjectBytes);
    if (aligned_size > Config::kMaxSmallObjectBytes) {
      // 调试构建已断言；发布构建下丢弃错误请求，避免越界
      return;
    }

    MemBlock** my_free_list = free_list_ + FreeListIndex(aligned_size);
    MemBlock* q = reinterpret_cast<MemBlock*>(p);
    q->next = *my_free_list;
    *my_free_list = q;
  }

  // 重新分配内存（调用方已持有锁）
  [[nodiscard]] void* ReallocateImpl(void* p, size_t old_size, size_t new_size) {
    if (p == nullptr) {
      return AllocateImpl(new_size);
    }
    void* result = AllocateImpl(new_size);
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    std::memcpy(result, p, copy_size);
    DeallocateImpl(p, old_size);
    return result;
  }

  // 向系统申请内存并分配给空闲链表
  [[nodiscard]] void* Refill(size_t n) {
    int nobjs = Config::kDefaultNobjs;

    char* chunk = ChunkAlloc(n, nobjs);
    assert(chunk != nullptr);

    if (nobjs == 1) {
      return chunk;
    }

    MemBlock** my_free_list = free_list_ + FreeListIndex(n);
    MemBlock* result = reinterpret_cast<MemBlock*>(chunk);
    *my_free_list = reinterpret_cast<MemBlock*>(chunk + n);
    MemBlock* current = *my_free_list;

    for (int i = 1;; ++i) {
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
  [[nodiscard]] char* ChunkAlloc(size_t size, int& nobjs) {
    char* result;
    size_t total_bytes = size * nobjs;
    // 起始指针为空时，剩余空间视为 0（避免对空指针做减法）
    size_t bytes_left = (start_free_ == nullptr) ? 0 : (end_free_ - start_free_);

    if (bytes_left >= total_bytes) {
      result = start_free_;
      start_free_ += total_bytes;
      return result;
    } else if (bytes_left >= size) {
      nobjs = static_cast<int>(bytes_left / size);
      total_bytes = size * nobjs;
      result = start_free_;
      start_free_ += total_bytes;
      return result;
    } else {
      size_t bytes_to_get = 2 * total_bytes + RoundUp(heap_size_ >> 4);

      if (bytes_left > 0) {
        MemBlock** my_free_list = free_list_ + FreeListIndex(bytes_left);
        reinterpret_cast<MemBlock*>(start_free_)->next = *my_free_list;
        *my_free_list = reinterpret_cast<MemBlock*>(start_free_);
      }

      char* new_chunk = reinterpret_cast<char*>(std::malloc(bytes_to_get));
      if (new_chunk == nullptr) {
        for (size_t i = size; i <= Config::kMaxSmallObjectBytes; i += Config::kAlignSize) {
          MemBlock** my_free_list = free_list_ + FreeListIndex(i);
          MemBlock* p = *my_free_list;
          if (p != nullptr) {
            *my_free_list = p->next;
            start_free_ = reinterpret_cast<char*>(p);
            end_free_ = start_free_ + i;
            return ChunkAlloc(size, nobjs);
          }
        }
        throw std::bad_alloc();
      }

      // 记录新申请的内存块，供析构时回收
      auto* node = static_cast<ChunkNode*>(std::malloc(sizeof(ChunkNode)));
      if (node == nullptr) {
        std::free(new_chunk);
        throw std::bad_alloc();
      }
      node->base = new_chunk;
      node->size = bytes_to_get;
      node->next = chunk_list_;
      chunk_list_ = node;

      start_free_ = new_chunk;
      heap_size_ += bytes_to_get;
      end_free_ = start_free_ + bytes_to_get;
      return ChunkAlloc(size, nobjs);
    }
  }
};

// 内存池主类（根据内存大小自动选择一级或二级分配器）
template <typename Config = DefaultConfig>
class MemoryPool {
 private:
  FirstLevelAllocator<Config> first_level_alloc_;
  SecondLevelAllocator<Config> second_level_alloc_;

 public:
  // 构造函数
  MemoryPool() = default;

  // 禁止拷贝构造和赋值操作
  MemoryPool(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;

  // 分配内存
  [[nodiscard]] void* Allocate(size_t n) {
    if (n > Config::kMaxSmallObjectBytes) {
      void* result = first_level_alloc_.Allocate(n);
      if (result == nullptr) {
        throw std::bad_alloc();
      }
      return result;
    } else {
      return second_level_alloc_.Allocate(n);
    }
  }

  // 释放内存
  void Deallocate(void* p, size_t n) noexcept {
    if (p == nullptr) {
      return;
    }

    if (n > Config::kMaxSmallObjectBytes) {
      first_level_alloc_.Deallocate(p, n);
    } else {
      second_level_alloc_.Deallocate(p, n);
    }
  }

  // 重新分配内存
  [[nodiscard]] void* Reallocate(void* p, size_t old_size, size_t new_size) {
    if (p == nullptr) {
      return Allocate(new_size);
    }

    if (old_size > Config::kMaxSmallObjectBytes && new_size > Config::kMaxSmallObjectBytes) {
      void* result = first_level_alloc_.Reallocate(p, old_size, new_size);
      if (result == nullptr) {
        throw std::bad_alloc();
      }
      return result;
    } else if (new_size <= Config::kMaxSmallObjectBytes) {
      void* new_p = Allocate(new_size);
      size_t copy_size = (old_size < new_size) ? old_size : new_size;
      std::memcpy(new_p, p, copy_size);
      Deallocate(p, old_size);
      return new_p;
    } else {
      void* new_p = Allocate(new_size);
      std::memcpy(new_p, p, old_size);
      Deallocate(p, old_size);
      return new_p;
    }
  }

  // 当前池向系统申请的总字节数（用于诊断/测试）
  [[nodiscard]] size_t heap_size() const noexcept {
    return second_level_alloc_.heap_size();
  }
};

// 全局内存池实例
extern MemoryPool<> default_memory_pool;

// 内存分配器类型（符合 STL 分配器要求）
template <typename T, typename Config = DefaultConfig>
class Allocator {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using propagate_on_container_move_assignment = std::true_type;

  // 构造函数
  constexpr Allocator() noexcept = default;

  // 拷贝构造函数
  template <typename U>
  constexpr Allocator(const Allocator<U, Config>&) noexcept {}

  // 分配内存
  [[nodiscard]] T* allocate(size_type n) {
    if (n > std::numeric_limits<size_type>::max() / sizeof(T)) {
      throw std::bad_alloc();
    }
    return static_cast<T*>(default_memory_pool.Allocate(n * sizeof(T)));
  }

  // 释放内存
  void deallocate(T* p, size_type n) noexcept {
    default_memory_pool.Deallocate(p, n * sizeof(T));
  }
};

// 分配器相等性比较
template <typename T, typename U, typename Config>
constexpr bool operator==(const Allocator<T, Config>&, const Allocator<U, Config>&) noexcept {
  return true;
}

// 分配器不等性比较
template <typename T, typename U, typename Config>
constexpr bool operator!=(const Allocator<T, Config>&, const Allocator<U, Config>&) noexcept {
  return false;
}

}  // namespace memory_pool

// 重载全局 new/delete 以使用内存池。
// 说明：所有从全局 new/delete 分配的内存，都在用户指针前放置一个头部，
// 记录真实的底层基址与总字节数，这样无参 delete 也能正确回收
//（避免了旧实现用 sizeof(void*) 作为尺寸而导致的错误分桶/污染）。
void* operator new(size_t size);
void* operator new(size_t size, std::align_val_t alignment);
void operator delete(void* p) noexcept;
void operator delete(void* p, size_t size) noexcept;
void operator delete(void* p, std::align_val_t alignment) noexcept;
void operator delete(void* p, size_t size, std::align_val_t alignment) noexcept;
void* operator new[](size_t size);
void* operator new[](size_t size, std::align_val_t alignment);
void operator delete[](void* p) noexcept;
void operator delete[](void* p, size_t size) noexcept;
void operator delete[](void* p, std::align_val_t alignment) noexcept;
void operator delete[](void* p, size_t size, std::align_val_t alignment) noexcept;

#endif  // MEMORY_POOL_MEMORY_POOL_HPP_
