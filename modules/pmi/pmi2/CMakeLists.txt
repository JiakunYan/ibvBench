target_include_directories(pmi-lib PUBLIC include)
target_sources(pmi-lib PRIVATE
        client/pmi2_util.c
        client/pmi2_api.c
        client/pmi2_util.h
        )

add_executable(pmi2-test client/test.c)
target_link_libraries(pmi2-test PRIVATE pmi-lib)
target_compile_definitions(pmi2-test PRIVATE _GNU_SOURCE)
set_target_properties(pmi2-test PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)