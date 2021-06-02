include_directories(..)
add_ibv_executable(ibv_pingpong_rdv_write ibv_pingpong_rdv_write.cpp)
target_link_libraries(ibv_pingpong_rdv_write PRIVATE mlog-lib pmi-lib)