// Copyright (c) 2026 Howland Mai
// 依据 MIT 许可证发布，详见 LICENSE 文件。

#include "test_utils.hpp"

#include <iostream>
#include <string>

// 测试套件入口：按需只跑某一组，例如 `./test_memory_pool basic`
//   ./test_memory_pool            运行全部用例
//   ./test_memory_pool basic      只运行 basic 组
//   ./test_memory_pool --quiet    静默模式（不打印用例过程信息）
int main(int argc, char **argv) {
  std::string filter;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--quiet" || arg == "-q") {
      memslice_test::Verbose() = false;
    } else {
      filter = arg;
    }
  }

  std::cout << "MemSlice Test Suite" << std::endl;
  std::cout << "===================" << std::endl;
  if (!filter.empty()) {
    std::cout << "filter: " << filter << std::endl;
  }

  const int failed = memslice_test::RunAll(filter);
  if (failed == 0) {
    std::cout << "\nAll tests passed!" << std::endl;
  }
  return (failed == 0) ? 0 : 1;
}
