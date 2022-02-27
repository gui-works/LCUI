target("pandagl")
    set_kind("static")
    add_files("src/*.c")
    add_deps("yutil")

target("pandagl_test")
    set_default(false)
    set_kind("binary")
    add_files("test/*.c")
    add_deps("ctest", "pandagl")