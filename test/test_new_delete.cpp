// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 全局 new/delete 重载、over-aligned 分配、STL 分配器。
#include "test_utils.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <list>
#include <string>
#include <thread>
#include <vector>

#include "./include/memory_pool.hpp"

using namespace memory_pool;

// 测试 new 和 delete 操作符重载
MEMSLICE_CASE(new_delete, operators) {
  std::cout << "\nTesting new and delete operators..." << std::endl;

  // 测试普通对象的 new 和 delete
  int *p1 = new int(42);
  assert(p1 != nullptr);
  assert(*p1 == 42);
  std::cout << "Allocated int with new: " << *p1 << std::endl;
  delete p1;

  // 测试数组的 new[] 和 delete[]
  int *p2 = new int[5];
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

    TestClass(int v, const std::string &n) : value(v), name(n) {}

    // 重载 new 和 delete 使用内存池
    static void *operator new(size_t size) { return DefaultMemoryPool().Allocate(size); }

    static void operator delete(void *p, size_t size) noexcept { DefaultMemoryPool().Deallocate(p, size); }
  };

  TestClass *p3 = new TestClass(100, "Test");
  assert(p3 != nullptr);
  assert(p3->value == 100);
  assert(p3->name == "Test");
  std::cout << "Allocated TestClass with new: " << p3->value << ", " << p3->name << std::endl;
  delete p3;

  std::cout << "New and delete operators test passed!" << std::endl;
}

// 测试超对齐类型的内存分配（回归：新增了对齐 new/delete 重载）
struct alignas(64) Aligned64 {
  char data[64];
};

MEMSLICE_CASE(new_delete, over_aligned) {
  std::cout << "\nTesting over-aligned allocation..." << std::endl;

  Aligned64 *p1 = new Aligned64();
  assert(p1 != nullptr);
  assert((reinterpret_cast<std::uintptr_t>(p1) % 64) == 0);
  std::cout << "Allocated alignas(64) object at " << p1 << " (mod 64 == " << (reinterpret_cast<std::uintptr_t>(p1) % 64)
            << ")" << std::endl;
  delete p1;

  Aligned64 *arr = new Aligned64[3];
  assert(arr != nullptr);
  assert((reinterpret_cast<std::uintptr_t>(arr) % 64) == 0);
  arr[1].data[0] = 42;
  assert(arr[1].data[0] == 42);
  std::cout << "Allocated alignas(64) array at " << arr << std::endl;
  delete[] arr;

  // 验证基本对齐对象仍正常（int / double）
  int *pi = new int(12345);
  assert(pi != nullptr && *pi == 12345);
  delete pi;
  double *pd = new double(3.14);
  assert(pd != nullptr && *pd > 3.0 && *pd < 4.0);
  delete pd;

  std::cout << "Over-aligned allocation test passed!" << std::endl;
}

// 32 字节对齐类型（用于 Allocator 对齐回归测试）
struct alignas(32) A32 {
  char data[32];
};

// 测试超对齐类型经 STL 分配器分配时的对齐（回归：此前池与 malloc 均只保证 16 字节对齐）
MEMSLICE_CASE(new_delete, over_aligned_allocator) {
  std::cout << "\nTesting over-aligned Allocator<T>..." << std::endl;

  {
    std::vector<A32, Allocator<A32>> v(3);
    assert((reinterpret_cast<std::uintptr_t>(v.data()) % 32) == 0);
    std::cout << "vector<A32> data aligned to 32 bytes" << std::endl;
  }

  {
    std::vector<Aligned64, Allocator<Aligned64>> v(3);
    assert((reinterpret_cast<std::uintptr_t>(v.data()) % 64) == 0);
    std::cout << "vector<Aligned64> data aligned to 64 bytes" << std::endl;
  }

  {
    // 16 对齐类型（普通档）也应满足
    std::vector<long double, Allocator<long double>> v(3);
    assert((reinterpret_cast<std::uintptr_t>(v.data()) % alignof(long double)) == 0);
    std::cout << "vector<long double> data aligned" << std::endl;
  }

  std::cout << "Over-aligned Allocator test passed!" << std::endl;
}

// 测试 STL 分配器兼容性
MEMSLICE_CASE(new_delete, stl_compatibility) {
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
  for (const auto &elem: my_list) {
    if (count % 100 == 0) {
      std::cout << "Element at position " << count << ": " << elem << std::endl;
    }
    count++;
  }

  std::cout << "STL compatibility test passed!" << std::endl;
}
