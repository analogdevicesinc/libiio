cmake_minimum_required(VERSION 3.13)
project(iiod-emu C)

add_executable(iiod-emu iiod-emu.c)
set_target_properties(
    iiod-emu
    PROPERTIES C_STANDARD 99
               C_STANDARD_REQUIRED ON
               C_EXTENSIONS OFF)
target_link_libraries(iiod-emu LINK_PRIVATE iio iio_common_config
                      ${PTHREAD_LIBRARIES})
target_include_directories(iiod-emu PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
                                            ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(iiod-emu PRIVATE _GNU_SOURCE)

if (WITH_GCOV)
    enable_coverage(iiod-emu)
endif ()

include(GNUInstallDirs)
if (NOT SKIP_INSTALL_ALL)
    install(TARGETS iiod-emu RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()
