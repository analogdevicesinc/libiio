cmake_minimum_required(VERSION 3.13)

add_library(vita49_2 vita49_2_packet_types.c vita49_2_packet_elements.c)

target_include_directories(vita49_2 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(vita49_2 PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

include(GNUInstallDirs)

install(TARGETS vita49_2
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/vita49_2_packet_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/vita49_2_packet_elements.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vita49_2
)