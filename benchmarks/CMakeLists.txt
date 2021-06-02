add_ibv_executable(ibv_pingpong_sendrecv ibv_pingpong_sendrecv.cpp)
target_link_libraries(ibv_pingpong_sendrecv PRIVATE mlog-lib pmi-lib)

add_ibv_executable(ibv_pingpong_write ibv_pingpong_write.cpp)
target_link_libraries(ibv_pingpong_write PRIVATE mlog-lib pmi-lib)

add_ibv_executable(ibv_pingpong_write_imm ibv_pingpong_write_imm.cpp)
target_link_libraries(ibv_pingpong_write_imm PRIVATE mlog-lib pmi-lib)

find_package(MPI)
if(MPI_FOUND)
    add_executable(mpi_pingpong mpi_pingpong.cpp)
    target_include_directories(mpi_pingpong PRIVATE ${MPI_CXX_INCLUDE_PATH})
    target_compile_options(mpi_pingpong PRIVATE ${MPI_CXX_COMPILE_FLAGS})
    target_link_libraries(mpi_pingpong PRIVATE ${MPI_CXX_LIBRARIES} ${MPI_CXX_LINK_FLAGS} mlog-lib)
endif()

add_subdirectory(rendezvous)