set(LCM_PM_BACKEND pmi1 CACHE STRING "Process management backend to use")
set_property(CACHE LCM_PM_BACKEND PROPERTY STRINGS pmi1 pmi2)

add_library(pmi-obj OBJECT)
set_target_properties(pmi-obj PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        )
if(LCM_PM_BACKEND STREQUAL "pmi1")
    target_sources(pmi-obj PRIVATE pmi_wrapper_pmi1.c)
    add_subdirectory(pmi1)
elseif(LCM_PM_BACKEND STREQUAL "pmi2")
    target_sources(pmi-obj PRIVATE pmi_wrapper_pmi2.c)
    add_subdirectory(pmi2)
elseif(LCM_PM_BACKEND STREQUAL "mpi")
    find_package(MPI REQUIRED)
    target_sources(pmi-obj PRIVATE pmi_wrapper_mpi.c)
    target_link_libraries(pmi-obj PRIVATE MPI::MPI_C)
#elseif(LCM_PM_BACKEND STREQUAL "file")
else()
    message(FATAL_ERROR "LCM_PM_BACKEND ${LCM_PM_BACKEND} not supported")
endif()

add_library(pmi_shared SHARED)
add_library(pmi_static STATIC)
target_link_libraries(pmi_shared PRIVATE pmi-obj)
target_link_libraries(pmi_static PRIVATE pmi-obj)
target_include_directories(pmi_shared PUBLIC .)
target_include_directories(pmi_static PUBLIC .)
