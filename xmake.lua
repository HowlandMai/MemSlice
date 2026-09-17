-- 设置全局工具链为 clang
set_toolchains("clang")

set_project("MemSlice")
set_version("1.0.0")
set_languages("c++20")

-- 静态库目标 memory_pool
target("memory_pool")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})

-- 可执行程序目标 test_memory_pool
target("test_memory_pool")
    set_kind("binary")
    add_files("test_memory_pool.cpp")
    add_deps("memory_pool")