target_include_directories(pmi-lib PUBLIC include)
target_sources(pmi-lib PRIVATE
        simple/simple_pmi.c
        simple/simple_pmiutil.c
)

add_executable(pmi-test simple/test.c)
target_link_libraries(pmi-test PRIVATE pmi-lib)
target_compile_definitions(pmi-test PRIVATE _GNU_SOURCE)
set_target_properties(pmi-test PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)
