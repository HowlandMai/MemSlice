// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#include <iostream>
#include <cassert>
#include <vector>
#include <chrono>
#include <string>
#include <list>
#include <thread>
#include <cstdint>
#include "./include/memory_pool.hpp"

// 使用内存池命名空间
using namespace memory_pool;

// 测试基本的内存分配和释放功能
void TestBasicAllocateDeallocate() {
  std::cout << "Testing basic allocate/deallocate..." << std::endl;

  // 使用默认配置的内存池
  MemoryPool<> pool;

  // 测试小内存分配
  void* p1 = pool.Allocate(16);
  assert(p1 != nullptr);
  std::cout << "Allocated 16 bytes at " << p1 << std::endl;

  void* p2 = pool.Allocate(32);
  assert(p2 != nullptr);
  std::cout << "Allocated 32 bytes at " << p2 << std::endl;

  void* p3 = pool.Allocate(64);
  assert(p3 != nullptr);
  std::cout << "Allocated 64 bytes at " << p3 << std::endl;

  // 测试释放内存
  pool.Deallocate(p1, 16);
  std::cout << "Deallocated 16 bytes at " << p1 << std::endl;

  pool.Deallocate(p2, 32);
  std::cout << "Deallocated 32 bytes at " << p2 << std::endl;

  pool.Deallocate(p3, 64);
  std::cout << "Deallocated 64 bytes at " << p3 << std::endl;

  // 测试重复分配相同大小的内存（应该重用之前释放的内存）
  void* p4 = pool.Allocate(16);
  assert(p4 != nullptr);
  std::cout << "Reallocated 16 bytes at " << p4 << std::endl;

  pool.Deallocate(p4, 16);

  // 测试大于配置的最大小对象大小的内存分配（应该使用一级分配器）
  void* p_large = pool.Allocate(2048);
  assert(p_large != nullptr);
  std::cout << "Allocated large memory (2048 bytes) at " << p_large << std::endl;

  pool.Deallocate(p_large, 2048);
  std::cout << "Deallocated large memory at " << p_large << std::endl;

  std::cout << "Basic allocate/deallocate test passed!" << std::endl;
}

// 自定义配置结构体
struct CustomConfig {
  static constexpr size_t kAlignSize = 16;                  // 16 字节对齐
  static constexpr size_t kMaxSmallObjectBytes = 128;       // 最大小对象 128 字节
  static constexpr int kDefaultNobjs = 50;                  // 默认每次分配 50 个对象
};

// 测试自定义配置的内存池
void TestCustomConfig() {
  std::cout << "\nTesting custom configuration..." << std::endl;

  // 使用自定义配置的内存池
  MemoryPool<CustomConfig> pool;

  // 测试分配
  void* p1 = pool.Allocate(128);  // 刚好是最大小对象大小
  assert(p1 != nullptr);
  std::cout << "Allocated 128 bytes (custom max size) at " << p1 << std::endl;

  pool.Deallocate(p1, 128);

  std::cout << "Custom configuration test passed!" << std::endl;
}

// 测试 new 和 delete 操作符重载
void TestNewDeleteOperators() {
  std::cout << "\nTesting new and delete operators..." << std::endl;

  // 测试普通对象的 new 和 delete
  int* p1 = new int(42);
  assert(p1 != nullptr);
  assert(*p1 == 42);
  std::cout << "Allocated int with new: " << *p1 << std::endl;
  delete p1;

  // 测试数组的 new[] 和 delete[]
  int* p2 = new int[5];
  assert(p2 != nullptr);
  for (int i = 0; i < 5; ++i) {
    p2[i] = i * 10;
  }
  std::cout << "Allocated int array with new[]: ";
  for (int i = 0; i < 5; ++i) {
    std::cout << p2[i] << " ";
  }
  std::cout << std::endl;
  delete[] p2;

  // 测试自定义类的 new 和 delete
  class TestClass {
  public:
    int value;
    std::string name;

    TestClass(int v, const std::string& n) : value(v), name(n) {}

    // 重载 new 和 delete 使用内存池
    static void* operator new(size_t size) {
      return default_memory_pool.Allocate(size);
    }

    static void operator delete(void* p, size_t size) noexcept {
      default_memory_pool.Deallocate(p, size);
    }
  };

  TestClass* p3 = new TestClass(100, "Test");
  assert(p3 != nullptr);
  assert(p3->value == 100);
  assert(p3->name == "Test");
  std::cout << "Allocated TestClass with new: " << p3->value << ", " << p3->name << std::endl;
  delete p3;

  std::cout << "New and delete operators test passed!" << std::endl;
}

// 测试内存重新分配功能
void TestReallocate() {
  std::cout << "\nTesting reallocate..." << std::endl;

  MemoryPool<> pool;

  // 分配初始内存
  void* p1 = pool.Allocate(16);
  assert(p1 != nullptr);
  std::cout << "Allocated 16 bytes at " << p1 << std::endl;

  // 向 p1 写入一些数据
  int* data = static_cast<int*>(p1);
  *data = 12345;

  // 重新分配为更大的内存
  void* p2 = pool.Reallocate(p1, 16, 32);
  assert(p2 != nullptr);
  std::cout << "Reallocated to 32 bytes at " << p2 << std::endl;

  // 验证数据是否被正确拷贝
  assert(*static_cast<int*>(p2) == 12345);
  std::cout << "Data preserved after reallocation: " << *static_cast<int*>(p2) << std::endl;

  // 重新分配为更小的内存
  void* p3 = pool.Reallocate(p2, 32, 8);
  assert(p3 != nullptr);
  std::cout << "Reallocated to 8 bytes at " << p3 << std::endl;

  pool.Deallocate(p3, 8);

  std::cout << "Reallocate test passed!" << std::endl;
}

// 测试内存池的性能
void TestPerformance() {
  std::cout << "\nTesting performance..." << std::endl;

  const int kNumAllocations = 100000;

  // 测试内存池性能
  MemoryPool<> pool;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 64) + 1;  // 1 到 64 字节
    void* p = pool.Allocate(size);
    pool.Deallocate(p, size);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto pool_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "Memory pool: " << pool_duration.count() << " ms for " << kNumAllocations << " allocations/deallocations" << std::endl;

  // 测试系统 malloc 性能
  start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 64) + 1;  // 1 到 64 字节
    void* p = std::malloc(size);
    std::free(p);
  }

  end = std::chrono::high_resolution_clock::now();
  auto malloc_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "System malloc: " << malloc_duration.count() << " ms for " << kNumAllocations << " allocations/deallocations" << std::endl;

  std::cout << "Performance test completed!" << std::endl;
}

// 测试大量小内存分配
void TestLargeNumberOfAllocations() {
  std::cout << "\nTesting large number of allocations..." << std::endl;

  MemoryPool<> pool;
  const int kNumAllocations = 10000;
  std::vector<void*> pointers;
  pointers.reserve(kNumAllocations);

  // 分配大量小内存
  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 32) + 1;  // 1 到 32 字节
    void* p = pool.Allocate(size);
    pointers.push_back(p);
  }

  std::cout << "Allocated " << kNumAllocations << " small memory blocks" << std::endl;

  // 释放所有内存
  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 32) + 1;
    pool.Deallocate(pointers[i], size);
  }

  std::cout << "Freed all memory blocks" << std::endl;
  std::cout << "Large number of allocations test passed!" << std::endl;
}

// 测试 STL 分配器兼容性
void TestSTLCompatibility() {
  std::cout << "\nTesting STL compatibility..." << std::endl;

  // 使用内存池分配器创建 std::list
  std::list<int, Allocator<int>> my_list;

  // 向 list 中添加元素
  for (int i = 0; i < 1000; ++i) {
    my_list.push_back(i);
  }

  // 验证元素数量
  assert(my_list.size() == 1000);
  std::cout << "Created std::list with 1000 elements using memory pool allocator" << std::endl;

  // 测试访问元素
  int count = 0;
  for (const auto& elem : my_list) {
    if (count % 100 == 0) {
      std::cout << "Element at position " << count << ": " << elem << std::endl;
    }
    count++;
  }

  std::cout << "STL compatibility test passed!" << std::endl;
}

// 测试零字节分配（回归：此前会因索引无符号下溢导致越界崩溃）
void TestZeroSizeAllocation() {
  std::cout << "\nTesting zero-size allocation..." << std::endl;

  MemoryPool<> pool;
  void* a = pool.Allocate(0);
  void* b = pool.Allocate(0);
  assert(a != nullptr);
  assert(b != nullptr);
  assert(a != b);
  std::cout << "Allocated two distinct zero-size pointers: " << a << " vs " << b << std::endl;
  pool.Deallocate(a, 0);
  pool.Deallocate(b, 0);

  std::cout << "Zero-size allocation test passed!" << std::endl;
}

// 测试超对齐类型的内存分配（回归：新增了对齐 new/delete 重载）
struct alignas(64) Aligned64 {
  char data[64];
};

void TestOverAlignedAllocation() {
  std::cout << "\nTesting over-aligned allocation..." << std::endl;

  Aligned64* p1 = new Aligned64();
  assert(p1 != nullptr);
  assert((reinterpret_cast<std::uintptr_t>(p1) % 64) == 0);
  std::cout << "Allocated alignas(64) object at " << p1
            << " (mod 64 == " << (reinterpret_cast<std::uintptr_t>(p1) % 64) << ")" << std::endl;
  delete p1;

  Aligned64* arr = new Aligned64[3];
  assert(arr != nullptr);
  assert((reinterpret_cast<std::uintptr_t>(arr) % 64) == 0);
  arr[1].data[0] = 42;
  assert(arr[1].data[0] == 42);
  std::cout << "Allocated alignas(64) array at " << arr << std::endl;
  delete[] arr;

  // 验证基本对齐对象仍正常（int / double）
  int* pi = new int(12345);
  assert(pi != nullptr && *pi == 12345);
  delete pi;
  double* pd = new double(3.14);
  assert(pd != nullptr && *pd > 3.0 && *pd < 4.0);
  delete pd;

  std::cout << "Over-aligned allocation test passed!" << std::endl;
}

// 测试错误尺寸释放的防御行为（回归：不再静默越界分桶）
void TestWrongSizeDeallocation() {
  std::cout << "\nTesting wrong-size deallocation (defensive)..." << std::endl;

  MemoryPool<> pool;
  void* p = pool.Allocate(64);
  assert(p != nullptr);
  // 用错误的尺寸释放（64 字节块被当 8 字节释放）——调试构建下断言会拦截；这里仅验证不崩溃
  pool.Deallocate(p, 8);
  // 再从 64 字节桶分配，不应拿到被误放入 8 字节桶的同一指针
  void* q = pool.Allocate(64);
  assert(q != nullptr);
  std::cout << "Wrong-size deallocation survived; reallocated 64B at " << q << std::endl;
  pool.Deallocate(q, 64);

  std::cout << "Wrong-size deallocation test passed!" << std::endl;
}

// 测试并发安全（回归：默认开启线程安全，且无递归锁）
void TestThreadSafety() {
  std::cout << "\nTesting thread safety..." << std::endl;

  constexpr int kThreads = 4;
  constexpr int kOps = 50000;

  // 共享同一个池，多个线程同时分配/释放
  MemoryPool<> pool;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&pool]() {
      for (int i = 0; i < kOps; ++i) {
        size_t size = (i % 64) + 1;
        void* p = pool.Allocate(size);
        pool.Deallocate(p, size);
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }

  std::cout << "Completed " << (kThreads * kOps) << " concurrent alloc/dealloc, "
            << "pool heap_size=" << pool.heap_size() << " bytes" << std::endl;
  assert(pool.heap_size() > 0);
  std::cout << "Thread safety test passed!" << std::endl;
}

// 测试关闭线程安全的配置路径（覆盖空操作锁的编译与运行）
struct NoThreadSafeConfig {
  static constexpr size_t kAlignSize = 8;
  static constexpr size_t kMaxSmallObjectBytes = 128;
  static constexpr int kDefaultNobjs = 20;
  static constexpr bool kThreadSafe = false;
};

void TestNoThreadSafeConfig() {
  std::cout << "\nTesting kThreadSafe=false configuration..." << std::endl;

  MemoryPool<NoThreadSafeConfig> pool;
  void* p = pool.Allocate(32);
  assert(p != nullptr);
  pool.Deallocate(p, 32);
  void* q = pool.Allocate(0);
  assert(q != nullptr);
  pool.Deallocate(q, 0);
  std::cout << "NoThreadSafe configuration test passed!" << std::endl;
}

int main() {
  std::cout << "Memory Pool Test Suite" << std::endl;
  std::cout << "=====================" << std::endl;

  TestBasicAllocateDeallocate();
  TestCustomConfig();
  TestNewDeleteOperators();
  TestReallocate();
  TestPerformance();
  TestLargeNumberOfAllocations();
  TestSTLCompatibility();
  TestZeroSizeAllocation();
  TestOverAlignedAllocation();
  TestWrongSizeDeallocation();
  TestThreadSafety();
  TestNoThreadSafeConfig();

  std::cout << "\nAll tests passed!" << std::endl;
  return 0;
}
