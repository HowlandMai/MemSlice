-- mempool-lab 构建脚本
set_toolchains("clang")

set_project("mempool-lab")
set_version("1.0.0")
set_languages("c++20")

add_rules("mode.debug", "mode.release")

-- 源码文件头版权声明与 LICENSE 一致；风格遵循 Google C++（见 .clang-format）
set_warnings("all", "extra")

target("mempool_lab")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_cxxflags("-Wpedantic")

-- 基准（单独可执行，不纳入默认测试）
target("bench_mempool_lab")
    set_kind("binary")
    add_deps("mempool_lab")
    add_files("bench/*.cpp")

-- 测试：xmake test 门禁
target("test_mempool_lab")
    set_kind("binary")
    add_deps("mempool_lab")
    add_files("test/test_main.cpp", "test/test_basic.cpp", "test/test_realloc.cpp",
              "test/test_audit.cpp", "test/test_fuzz.cpp", "test/test_concurrency.cpp")
    add_includedirs("test")
    add_tests("default", {runargs = {"--quiet"}})
