// Copyright (c) 2026 Howland Mai
// SPDX-License-Identifier: MIT

// 测试入口：遍历注册表执行用例；支持按组名过滤与 --quiet。

#include <cstdio>
#include <cstring>
#include <string>

#include "test_utils.hpp"

int main(int argc, char **argv) {
  const char *filter = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quiet") == 0) {
      labtest::Verbose() = false;
    } else if (std::strncmp(argv[i], "--", 2) != 0) {
      filter = argv[i];
    }
  }

  int passed = 0;
  int failed = 0;
  int skipped = 0;

  std::printf("mempool-lab test suite\n======================\n");
  if (filter != nullptr) {
    std::printf("filter: %s\n", filter);
  }

  for (const auto &c: labtest::Registry()) {
    if (filter != nullptr && std::strcmp(c.group, filter) != 0) {
      ++skipped;
      continue;
    }
    std::printf("[ RUN  ] %s.%s\n", c.group, c.name);
    labtest::CurrentFailed() = false;
    c.body();
    if (labtest::CurrentFailed()) {
      std::printf("[ FAIL ] %s.%s\n", c.group, c.name);
      ++failed;
    } else {
      std::printf("[  OK  ] %s.%s\n", c.group, c.name);
      ++passed;
    }
  }

  std::printf("\n%d passed, %d failed", passed, failed);
  if (skipped > 0) {
    std::printf(", %d skipped (filter: %s)", skipped, filter);
  }
  std::printf("\n");
  if (failed == 0) {
    std::printf("\nAll tests passed!\n");
  }
  return failed == 0 ? 0 : 1;
}
