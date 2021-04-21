function(add_ibv_executable EXEC)
    add_executable(${EXEC} ${ARGN})
    target_link_libraries(${EXEC} PRIVATE Fabric::IBV)
    #    set_target_properties(${EXEC} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    install(TARGETS ${EXEC} DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endfunction()

