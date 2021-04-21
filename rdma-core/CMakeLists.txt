# Shared example files
add_library(ibverbs_tools STATIC
  pingpong.c
  )
target_link_libraries(ibverbs_tools PUBLIC Fabric::IBV)

#add_ibv_executable(ibv_asyncwatch asyncwatch.c)

add_ibv_executable(ibv_devices device_list.c)

#add_ibv_executable(ibv_devinfo devinfo.c)

add_ibv_executable(ibv_rc_pingpong rc_pingpong.c)
target_link_libraries(ibv_rc_pingpong PRIVATE ibverbs_tools)

add_ibv_executable(ibv_srq_pingpong srq_pingpong.c)
target_link_libraries(ibv_srq_pingpong PRIVATE ibverbs_tools)

add_ibv_executable(ibv_uc_pingpong uc_pingpong.c)
target_link_libraries(ibv_uc_pingpong PRIVATE ibverbs_tools)

add_ibv_executable(ibv_ud_pingpong ud_pingpong.c)
target_link_libraries(ibv_ud_pingpong PRIVATE ibverbs_tools)

add_ibv_executable(ibv_xsrq_pingpong xsrq_pingpong.c)
target_link_libraries(ibv_xsrq_pingpong PRIVATE ibverbs_tools)
