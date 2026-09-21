-- mempool-lab 构建脚本
set_toolchains("clang")
set_project("mempool-lab")
set_version("1.0.0")
set_languages("c++20")

add_rules("mode.debug", "mode.release")
set_warnings("all", "extra")

-- 核心静态库。src 下可能暂时为空——xmake 对空 glob 只警告不报错，
-- 因此这个 target 在最早期也能构建通过。
target("mempool_lab")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_cxxflags("-Wpedantic")
    if is_plat("linux") then
        add_syslinks("pthread")
    end
