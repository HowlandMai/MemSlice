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

#include <iostream>
#include <cassert>
#include <vector>
#include <chrono>
#include <string>
#include <list>
#include "./include/memory_pool.hpp"

// 使用内存池命名空间
// Use memory pool namespace
using namespace memory_pool;

// 测试基本的内存分配和释放功能
// Test basic memory allocation and deallocation functions
void TestBasicAllocateDeallocate() {
  std::cout << "Testing basic allocate/deallocate..." << std::endl;
  
  // 使用默认配置的内存池
  // Use memory pool with default configuration
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
  // Test reallocating the same size (should reuse previously freed memory)
  void* p4 = pool.Allocate(16);
  assert(p4 != nullptr);
  std::cout << "Reallocated 16 bytes at " << p4 << std::endl;
  
  pool.Deallocate(p4, 16);
  
  // 测试大于配置的最大小对象大小的内存分配（应该使用一级分配器）
  // Test allocation larger than max small object size (should use first level allocator)
  void* p_large = pool.Allocate(2048);
  assert(p_large != nullptr);
  std::cout << "Allocated large memory (2048 bytes) at " << p_large << std::endl;
  
  pool.Deallocate(p_large, 2048);
  std::cout << "Deallocated large memory at " << p_large << std::endl;
  
  std::cout << "Basic allocate/deallocate test passed!" << std::endl;
}

// 自定义配置结构体
// Custom configuration structure
struct CustomConfig {
  static constexpr size_t kAlignSize = 16;                  // 16字节对齐
  static constexpr size_t kMaxSmallObjectBytes = 128;       // 最大小对象128字节
  static constexpr size_t kDefaultNobjs = 50;               // 默认每次分配50个对象
};

// 测试自定义配置的内存池
// Test memory pool with custom configuration
void TestCustomConfig() {
  std::cout << "\nTesting custom configuration..." << std::endl;
  
  // 使用自定义配置的内存池
  // Use memory pool with custom configuration
  MemoryPool<CustomConfig> pool;
  
  // 测试分配
  void* p1 = pool.Allocate(128);  // 刚好是最大小对象大小
  assert(p1 != nullptr);
  std::cout << "Allocated 128 bytes (custom max size) at " << p1 << std::endl;
  
  pool.Deallocate(p1, 128);
  
  std::cout << "Custom configuration test passed!" << std::endl;
}

// 测试new和delete操作符重载
// Test new and delete operator overloading
void TestNewDeleteOperators() {
  std::cout << "\nTesting new and delete operators..." << std::endl;
  
  // 测试普通对象的new和delete
  int* p1 = new int(42);
  assert(p1 != nullptr);
  assert(*p1 == 42);
  std::cout << "Allocated int with new: " << *p1 << std::endl;
  delete p1;
  
  // 测试数组的new[]和delete[]
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
  
  // 测试自定义类的new和delete
  class TestClass {
  public:
    int value;
    std::string name;
    
    TestClass(int v, const std::string& n) : value(v), name(n) {}
    
    // 重载new和delete使用内存池
    // Overload new and delete to use memory pool
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
// Test memory reallocation function
void TestReallocate() {
  std::cout << "\nTesting reallocate..." << std::endl;
  
  MemoryPool<> pool;
  
  // 分配初始内存
  void* p1 = pool.Allocate(16);
  assert(p1 != nullptr);
  std::cout << "Allocated 16 bytes at " << p1 << std::endl;
  
  // 向p1写入一些数据
  // Write some data to p1
  int* data = static_cast<int*>(p1);
  *data = 12345;
  
  // 重新分配为更大的内存
  void* p2 = pool.Reallocate(p1, 16, 32);
  assert(p2 != nullptr);
  std::cout << "Reallocated to 32 bytes at " << p2 << std::endl;
  
  // 验证数据是否被正确拷贝
  // Verify data was correctly copied
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
// Test memory pool performance
void TestPerformance() {
  std::cout << "\nTesting performance..." << std::endl;
  
  const int kNumAllocations = 100000;
  
  // 测试内存池性能
  MemoryPool<> pool;
  auto start = std::chrono::high_resolution_clock::now();
  
  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 64) + 1;  // 1到64字节
    void* p = pool.Allocate(size);
    pool.Deallocate(p, size);
  }
  
  auto end = std::chrono::high_resolution_clock::now();
  auto pool_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "Memory pool: " << pool_duration.count() << " ms for " << kNumAllocations << " allocations/deallocations" << std::endl;
  
  // 测试系统malloc性能
  start = std::chrono::high_resolution_clock::now();
  
  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 64) + 1;  // 1到64字节
    void* p = std::malloc(size);
    std::free(p);
  }
  
  end = std::chrono::high_resolution_clock::now();
  auto malloc_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "System malloc: " << malloc_duration.count() << " ms for " << kNumAllocations << " allocations/deallocations" << std::endl;
  
  std::cout << "Performance test completed!" << std::endl;
}

// 测试大量小内存分配
// Test large number of small memory allocations
void TestLargeNumberOfAllocations() {
  std::cout << "\nTesting large number of allocations..." << std::endl;
  
  MemoryPool<> pool;
  const int kNumAllocations = 10000;
  std::vector<void*> pointers;
  pointers.reserve(kNumAllocations);
  
  // 分配大量小内存
  for (int i = 0; i < kNumAllocations; ++i) {
    size_t size = (i % 32) + 1;  // 1到32字节
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

// 测试STL分配器兼容性
// Test STL allocator compatibility
void TestSTLCompatibility() {
  std::cout << "\nTesting STL compatibility..." << std::endl;
  
  // 使用内存池分配器创建std::list
  // Create std::list using memory pool allocator
  std::list<int, Allocator<int>> my_list;
  
  // 向list中添加元素
  // Add elements to the list
  for (int i = 0; i < 1000; ++i) {
    my_list.push_back(i);
  }
  
  // 验证元素数量
  // Verify element count
  assert(my_list.size() == 1000);
  std::cout << "Created std::list with 1000 elements using memory pool allocator" << std::endl;
  
  // 测试访问元素
  // Test accessing elements
  int count = 0;
  for (const auto& elem : my_list) {
    if (count % 100 == 0) {
      std::cout << "Element at position " << count << ": " << elem << std::endl;
    }
    count++;
  }
  
  std::cout << "STL compatibility test passed!" << std::endl;
}

int main() {
  std::cout << "Memory Pool Test Suite" << std::endl;
  std::cout << "====================" << std::endl;
  
  TestBasicAllocateDeallocate();
  TestCustomConfig();
  TestNewDeleteOperators();
  TestReallocate();
  TestPerformance();
  TestLargeNumberOfAllocations();
  TestSTLCompatibility();
  
  std::cout << "\nAll tests passed!" << std::endl;
  return 0;
}