include_directories(..)
link_libraries(mlog-lib pmi-lib)
add_ibv_executable(ibv_pingpong_rdv_write ibv_pingpong_rdv_write.cpp)
add_ibv_executable(ibv_pingpong_rdv_read ibv_pingpong_rdv_read.cpp)
