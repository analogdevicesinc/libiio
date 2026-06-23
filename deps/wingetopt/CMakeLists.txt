cmake_minimum_required(VERSION 3.5...3.31)
project(wingetopt VERSION 1.0.0 LANGUAGES C)

string(REGEX REPLACE "/W[3|4]" "/w4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Define source directory
set(WINGETOPT_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Library target
add_library(wingetopt ${WINGETOPT_SRC_DIR}/getopt.c)

# Include directories
target_include_directories(wingetopt PUBLIC
    $<BUILD_INTERFACE:${WINGETOPT_SRC_DIR}>
    $<INSTALL_INTERFACE:include>
)
# Definitions
target_compile_definitions(wingetopt PRIVATE -D_CRT_SECURE_NO_WARNINGS)
if(BUILD_SHARED_LIBS)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_compile_definitions(wingetopt PRIVATE -DBUILDING_WINGETOPT_DLL -DWINGETOPT_SHARED_LIB)
endif()

# Install headers
install(FILES ${WINGETOPT_SRC_DIR}/getopt.h DESTINATION include)

# Install target
install(TARGETS wingetopt
    EXPORT wingetoptTargets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    INCLUDES DESTINATION include
)

# Export for config-based usage
install(EXPORT wingetoptTargets
    FILE wingetoptTargets.cmake
    NAMESPACE wingetopt::
    DESTINATION lib/cmake/wingetopt
)
