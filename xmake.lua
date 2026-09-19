-- 设置全局工具链为 clang
set_toolchains("clang")

set_project("MemSlice")
set_version("1.0.0")
set_languages("c++20")

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
target("test_memory_pool")
    set_kind("binary")
    add_files("test/test_main.cpp", "test/test_basic.cpp", "test/test_realloc.cpp",
              "test/test_new_delete.cpp", "test/test_guards.cpp", "test/test_concurrency.cpp")
    add_includedirs("test", ".")
    add_deps("memory_pool_global")

-- 性能基准（单独可执行，耗时长，不纳入主测试）
target("bench_memory_pool")
    set_kind("binary")
    add_files("test/test_main.cpp", "test/test_perf.cpp")
    add_includedirs("test", ".")
    add_deps("memory_pool_global")
