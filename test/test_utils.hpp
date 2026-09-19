// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#ifndef MEMORY_POOL_TEST_TEST_UTILS_HPP_
#define MEMORY_POOL_TEST_TEST_UTILS_HPP_

#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// 零依赖的极简测试框架：断言宏 + 用例自注册。
//
//   MEMSLICE_CASE(group, name) { ... }        注册一个用例（group 用作命令行过滤键）
//   MEMSLICE_EXPECT(cond)                     断言表达式为真，失败则退出当前用例
//   MEMSLICE_EXPECT_THROWS(expr, ExceptionT)  断言表达式抛出指定异常
//   MEMSLICE_INFO(msg)                        输出一条信息（仅在 verbose 模式下打印）
//
// 说明：断言失败不会终止进程，而是记录失败并跳到下一个用例（相比 assert 更容易定位）。

namespace memslice_test {

  // 用例条目
  struct TestCase {
    std::string group;
    std::string name;
    std::function<void()> fn;
  };

  // 全局注册表（函数内 static，避免静态初始化顺序问题）
  inline std::vector<TestCase> &Registry() {
    static std::vector<TestCase> registry;
    return registry;
  }

  // 当前用例的失败标记（供断言宏使用）
  inline bool &CurrentFailed() {
    static bool failed = false;
    return failed;
  }

  // 是否打印详细过程输出（由 main 依据命令行参数设置）
  inline bool &Verbose() {
    static bool verbose = true;
    return verbose;
  }

  // 用例自注册辅助对象
  class Registrar {
  public:
    Registrar(const char *group, const char *name, std::function<void()> fn) {
      Registry().push_back(TestCase{group, name, std::move(fn)});
    }
  };

  // 运行全部（或按 group 过滤）用例，返回失败个数
  inline int RunAll(const std::string &filter) {
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    for (const auto &tc: Registry()) {
      if (!filter.empty() && tc.group != filter) {
        ++skipped;
        continue;
      }
      std::cout << "[ RUN  ] " << tc.group << "." << tc.name << std::endl;
      CurrentFailed() = false;
      try {
        tc.fn();
      } catch (const std::exception &e) {
        std::cout << "         unexpected exception: " << e.what() << std::endl;
        CurrentFailed() = true;
      } catch (...) {
        std::cout << "         unexpected non-standard exception" << std::endl;
        CurrentFailed() = true;
      }
      if (CurrentFailed()) {
        std::cout << "[ FAIL ] " << tc.group << "." << tc.name << std::endl;
        ++failed;
      } else {
        std::cout << "[  OK  ] " << tc.group << "." << tc.name << std::endl;
        ++passed;
      }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed";
    if (skipped > 0) {
      std::cout << ", " << skipped << " skipped (filter: " << filter << ")";
    }
    std::cout << std::endl;
    return failed;
  }

} // namespace memslice_test

// 用例注册宏：文件内唯一名由 __LINE__ 保证
#define MEMSLICE_CASE(group, name)                                                                                     \
  static void MemsliceCase_##name();                                                                                   \
  static ::memslice_test::Registrar MemsliceRegistrar_##name(#group, #name, MemsliceCase_##name);                      \
  static void MemsliceCase_##name()

// 失败即输出位置信息并跳出当前用例
#define MEMSLICE_FAIL(msg)                                                                                             \
  do {                                                                                                                 \
    std::cout << "         FAILED at " << __FILE__ << ":" << __LINE__ << ": " << msg << std::endl;                     \
    ::memslice_test::CurrentFailed() = true;                                                                           \
    return;                                                                                                            \
  } while (false)

#define MEMSLICE_EXPECT(cond)                                                                                          \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      MEMSLICE_FAIL("expectation failed: " #cond);                                                                     \
    }                                                                                                                  \
  } while (false)

#define MEMSLICE_EXPECT_THROWS(expr, exception_type)                                                                   \
  do {                                                                                                                 \
    bool memslice_threw = false;                                                                                       \
    try {                                                                                                              \
      (void) (expr);                                                                                                   \
    } catch (const exception_type &) {                                                                                 \
      memslice_threw = true;                                                                                           \
    }                                                                                                                  \
    if (!memslice_threw) {                                                                                             \
      MEMSLICE_FAIL("expected " #exception_type " from: " #expr);                                                      \
    }                                                                                                                  \
  } while (false)

#define MEMSLICE_INFO(msg)                                                                                             \
  do {                                                                                                                 \
    if (::memslice_test::Verbose()) {                                                                                  \
      std::cout << "         " << msg << std::endl;                                                                    \
    }                                                                                                                  \
  } while (false)

#endif // MEMORY_POOL_TEST_TEST_UTILS_HPP_
