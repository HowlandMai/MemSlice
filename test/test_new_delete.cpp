// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

// 全局 new/delete 重载、over-aligned 分配、STL 分配器对齐。

#include "test_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "memory_pool/allocator.hpp"
#include "memory_pool/global_new.hpp"
#include "memory_pool/pool.hpp"

using namespace memory_pool;

// 测试 new 和 delete 操作符重载
MEMSLICE_CASE(new_delete, operators) {
  // 测试普通对象的 new 和 delete
  int *p1 = new int(42);
  MEMSLICE_EXPECT(p1 != nullptr);
  MEMSLICE_EXPECT(*p1 == 42);
  MEMSLICE_INFO("allocated int with new: " << *p1);
  delete p1;

  // 测试数组的 new[] 和 delete[]
  int *p2 = new int[5];
  MEMSLICE_EXPECT(p2 != nullptr);
  for (int i = 0; i < 5; ++i) {
    p2[i] = i * 10;
  }
  MEMSLICE_EXPECT(p2[4] == 40);
  delete[] p2;

  // 测试自定义类的 new 和 delete
  class TestClass {
  public:
    int value;
    std::string name;

    TestClass(int v, const std::string &n) : value(v), name(n) {}

    // 重载 new 和 delete 使用内存池（头部记账：释放无需尺寸）
    static void *operator new(size_t size) { return DefaultMemoryPool().Allocate(size); }

    static void operator delete(void *p) noexcept { DefaultMemoryPool().Deallocate(p); }

    static void operator delete(void *p, size_t /*size*/) noexcept { DefaultMemoryPool().Deallocate(p); }
  };

  TestClass *p3 = new TestClass(100, "Test");
  MEMSLICE_EXPECT(p3 != nullptr);
  MEMSLICE_EXPECT(p3->value == 100);
  MEMSLICE_EXPECT(p3->name == "Test");
  MEMSLICE_INFO("allocated TestClass with new: " << p3->value << ", " << p3->name);
  delete p3;
}

// 超对齐类型（用于对齐回归测试）
struct alignas(64) Aligned64 {
  char data[64];
};

// 32 字节对齐类型（用于 Allocator 对齐回归测试）
struct alignas(32) A32 {
  char data[32];
};

// 测试超对齐类型的内存分配（回归：新增了对齐 new/delete 重载）
MEMSLICE_CASE(new_delete, over_aligned) {
  Aligned64 *p1 = new Aligned64();
  MEMSLICE_EXPECT(p1 != nullptr);
  MEMSLICE_EXPECT((reinterpret_cast<std::uintptr_t>(p1) % 64) == 0);
  MEMSLICE_INFO("allocated alignas(64) object at " << p1 << " (mod 64 == 0)");
  delete p1;

  Aligned64 *arr = new Aligned64[3];
  MEMSLICE_EXPECT(arr != nullptr);
  MEMSLICE_EXPECT((reinterpret_cast<std::uintptr_t>(arr) % 64) == 0);
  arr[1].data[0] = 42;
  MEMSLICE_EXPECT(arr[1].data[0] == 42);
  MEMSLICE_INFO("allocated alignas(64) array at " << arr);
  delete[] arr;

  // 验证基本对齐对象仍正常（int / double）
  int *pi = new int(12345);
  MEMSLICE_EXPECT(pi != nullptr && *pi == 12345);
  delete pi;
  double *pd = new double(3.14);
  MEMSLICE_EXPECT(pd != nullptr && *pd > 3.0 && *pd < 4.0);
  delete pd;
}

// 测试超对齐类型经 STL 分配器分配时的对齐（回归：此前池与 malloc 均只保证 16 字节对齐）
MEMSLICE_CASE(new_delete, over_aligned_allocator) {
  {
    std::vector<A32, Allocator<A32>> v(3);
    MEMSLICE_EXPECT((reinterpret_cast<std::uintptr_t>(v.data()) % 32) == 0);
    MEMSLICE_INFO("vector<A32> data aligned to 32 bytes");
  }

  {
    std::vector<Aligned64, Allocator<Aligned64>> v(3);
    MEMSLICE_EXPECT((reinterpret_cast<std::uintptr_t>(v.data()) % 64) == 0);
    MEMSLICE_INFO("vector<Aligned64> data aligned to 64 bytes");
  }

  {
    // 16 对齐类型（普通档）也应满足
    std::vector<long double, Allocator<long double>> v(3);
    MEMSLICE_EXPECT((reinterpret_cast<std::uintptr_t>(v.data()) % alignof(long double)) == 0);
    MEMSLICE_INFO("vector<long double> data aligned");
  }
}

// 测试 STL 分配器兼容性
MEMSLICE_CASE(new_delete, stl_compatibility) {
  std::list<int, Allocator<int>> my_list;
  for (int i = 0; i < 1000; ++i) {
    my_list.push_back(i);
  }
  MEMSLICE_EXPECT(my_list.size() == 1000);

  int count = 0;
  for (const auto &elem: my_list) {
    MEMSLICE_EXPECT(elem == count);
    ++count;
  }
  MEMSLICE_INFO("created std::list with 1000 elements using memory pool allocator");
}
