-- mempool-lab 构建脚本
--
-- 目标设计：用 glob 通配而非逐个列文件。
-- 这样每新增一个源文件/测试文件无需改构建脚本，也让历史中的每个提交
-- 都能自洽地构建（不会出现「脚本引用了尚不存在的文件」）。

set_toolchains("clang")
set_project("mempool-lab")
set_version("1.0.0")
set_languages("c++20")

add_rules("mode.debug", "mode.release")

-- 源码文件头版权声明与 LICENSE 一致；风格遵循 Google C++（见 .clang-format）
set_warnings("all", "extra")

-- 静态库：核心实现（src 下可能暂时为空，glob 会自动适配）
target("mempool_lab")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_cxxflags("-Wpedantic")
    if is_plat("linux") then
        add_syslinks("pthread")
    end

-- 基准（单独可执行，不纳入默认测试）
target("bench_mempool_lab")
    set_kind("binary")
    add_deps("mempool_lab")
    add_files("bench/*.cpp")

-- 测试：xmake test 门禁（glob 覆盖 test/ 下所有用例文件）
target("test_mempool_lab")
    set_kind("binary")
    add_deps("mempool_lab")
    add_files("test/test_*.cpp")
    add_includedirs("test")
    add_tests("default", {runargs = {"--quiet"}})
