cmake_minimum_required(VERSION 3.13)

include(GNUInstallDirs)

# Helper for VITA49.2 runtime-loadable plugins
function(add_command_sequence_plugin target)
    add_library(${target} MODULE ${ARGN})

    target_link_libraries(${target} PRIVATE
        iio
        vita49_2
        iio_common_config
        m
    )

    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/vita49_2
        ${CMAKE_SOURCE_DIR}/iiod
        ${CMAKE_SOURCE_DIR}/iiod/vita49_2
        ${CMAKE_BINARY_DIR}
    )

    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        PREFIX ""   # plugin.so instead of libplugin.so
    )

    install(TARGETS ${target}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/iiod/vita49_2/plugins
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endfunction()

# One target per plugin .so
add_command_sequence_plugin(tdd_calibration
    tdd_calibration.c
)