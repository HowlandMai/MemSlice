// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

// 极简测试框架：零第三方依赖。
//
// 设计：用例通过静态注册表自注册，main 遍历执行；支持按组名过滤。
// 这样加用例只需写 MEMSLICE_CASE，无需改任何注册代码。

#ifndef MEMPOOL_LAB_TEST_UTILS_HPP_
#define MEMPOOL_LAB_TEST_UTILS_HPP_

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace labtest {

  struct Case {
    const char *group;
    const char *name;
    std::function<void()> body;
  };

  [[nodiscard]] inline std::vector<Case> &Registry() {
    static std::vector<Case> cases;
    return cases;
  }

  struct Registrar {
    Registrar(const char *group, const char *name, std::function<void()> body) {
      Registry().push_back({group, name, std::move(body)});
    }
  };

  inline bool &CurrentFailed() {
    static bool failed = false;
    return failed;
  }

  inline bool &Verbose() {
    static bool verbose = true;
    return verbose;
  }

} // namespace labtest

// 定义一个用例。group 用于命令行过滤（`./test_mempool_lab basic`）。
#define MEMPOOL_CASE(group, name)                                                                                      \
  static void MempoolCase_##group##_##name();                                                                          \
  static ::labtest::Registrar MempoolReg_##group##_##name(#group, #name, MempoolCase_##group##_##name);                \
  static void MempoolCase_##group##_##name()

#define MEMPOOL_EXPECT(cond)                                                                                           \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      std::printf("         FAILED at %s:%d: %s\n", __FILE__, __LINE__, #cond);                                        \
      ::labtest::CurrentFailed() = true;                                                                               \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (false)

#define MEMPOOL_EXPECT_THROWS(expr, extype)                                                                            \
  do {                                                                                                                 \
    bool threw = false;                                                                                                \
    try {                                                                                                              \
      (void) (expr);                                                                                                   \
    } catch (const extype &) {                                                                                         \
      threw = true;                                                                                                    \
    }                                                                                                                  \
    if (!threw) {                                                                                                      \
      std::printf("         FAILED at %s:%d: 期望抛出 %s\n", __FILE__, __LINE__, #extype);                             \
      ::labtest::CurrentFailed() = true;                                                                               \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (false)

#define MEMPOOL_INFO(msg)                                                                                              \
  do {                                                                                                                 \
    if (::labtest::Verbose()) {                                                                                        \
      std::printf("         %s\n", (std::string() + msg).c_str());                                                     \
    }                                                                                                                  \
  } while (false)

#endif // MEMPOOL_LAB_TEST_UTILS_HPP_
