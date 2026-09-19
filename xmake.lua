-- 设置全局工具链为 clang
set_toolchains("clang")

set_project("MemSlice")
set_version("1.0.0")
set_languages("c++20")

-- 调试期哨兵校验（默认关闭 = 零开销）。
-- 启用方式（用 -D 直接传给编译器，已验证生效）：
--     xmake f --cxflags="-DMEMSLICE_DEBUG_CHECKS=1" && xmake -r
-- 启用后 guards.sentinel_* 用例会真正执行检测（否则自我跳过）。
-- 说明：曾尝试用 option()+has_config() 包装，但在本环境（xmake 3.0.6）
-- 该组合读不到配置值，故改用最直接且可验证的 -D 方式。

-- 头文件库目标：纯接口，无全局 new/delete 副作用，仅提供 MemoryPool / Allocator<T>
target("memory_pool")
    set_kind("headeronly")
    add_headerfiles("include/(**.hpp)")
    add_includedirs("include", {public = true})

-- 全局 new/delete 重载目标（opt-in）：链接它才会让程序内所有堆分配走内存池
target("memory_pool_global")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("memory_pool")

-- 测试套件（依赖全局重载，因为用例覆盖 new/delete 重载路径）
-- 通过 add_tests 接入 xmake 测试框架：用例进程退出码非 0 即判定失败，
-- 于是 `xmake test` 把测试结果变成构建系统层面的成功/失败门禁。
target("test_memory_pool")
    set_kind("binary")
    add_files("test/test_main.cpp", "test/test_basic.cpp", "test/test_realloc.cpp",
              "test/test_new_delete.cpp", "test/test_guards.cpp", "test/test_concurrency.cpp")
    add_includedirs("test", ".")
    add_deps("memory_pool_global")
    add_tests("default", {runargs = {"--quiet"}})
    add_tests("basic", {runargs = {"basic", "--quiet"}})
    add_tests("realloc", {runargs = {"realloc", "--quiet"}})
    add_tests("new_delete", {runargs = {"new_delete", "--quiet"}})
    add_tests("guards", {runargs = {"guards", "--quiet"}})
    add_tests("concurrency", {runargs = {"concurrency", "--quiet"}})

-- 性能基准（单独可执行，耗时长，不纳入默认测试）
target("bench_memory_pool")
    set_kind("binary")
    add_files("test/test_main.cpp", "test/test_perf.cpp")
    add_includedirs("test", ".")
    add_deps("memory_pool_global")
