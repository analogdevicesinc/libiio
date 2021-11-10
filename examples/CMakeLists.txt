cmake_minimum_required(VERSION 2.8.7)

project(ad9361-iiostream C)
project(ad9371-iiostream C)
project(adrv9009-iiostream C)
project(dummy-iiostream C)
project(adrv9002-iiostream C)

if (APPLE)
    # Add relative rpath to iio library (which is in the same framework)
    set(CMAKE_INSTALL_RPATH "@executable_path/../..")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
endif ()

if (MSVC)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../deps/wingetopt/src)
    set(GETOPT_C_FILE
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/wingetopt/src/getopt.c
    )
endif (MSVC)

if (WIN32)
    set(LIBIIO_ORIGINAL_FILENAME)
    set(LIBIIO_RC ${CMAKE_CURRENT_BINARY_DIR}/properties.rc)
    configure_file(../properties.rc.cmakein ${LIBIIO_RC} @ONLY)
endif ()

if (CMAKE_COMPILER_IS_GNUCC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DIIO_CHECK_RET")
endif ()

add_executable(
    ad9361-iiostream ad9361-iiostream.c ${GETOPT_C_FILE} ${LIBIIO_RC}
)
add_executable(
    ad9371-iiostream ad9371-iiostream.c ${GETOPT_C_FILE} ${LIBIIO_RC}
)
add_executable(
    adrv9009-iiostream adrv9009-iiostream.c ${GETOPT_C_FILE} ${LIBIIO_RC}
)
add_executable(
    adrv9002-iiostream adrv9002-iiostream.c ${GETOPT_C_FILE} ${LIBIIO_RC}
)
add_executable(dummy-iiostream dummy-iiostream.c ${GETOPT_C_FILE} ${LIBIIO_RC})

target_link_libraries(ad9361-iiostream iio)
target_link_libraries(ad9371-iiostream iio)
target_link_libraries(adrv9009-iiostream iio)
target_link_libraries(dummy-iiostream iio)
target_link_libraries(adrv9002-iiostream iio)

set(IIO_EXAMPLES_TARGETS ad9361-iiostream ad9371-iiostream adrv9009-iiostream
                         dummy-iiostream adrv9002-iiostream
)

find_library(PTHREAD_LIBRARIES pthread)
find_library(CURSES_LIBRARY curses)
find_library(CDK_LIBRARY cdk)

include(CheckSymbolExists)

if (PTHREAD_LIBRARIES
    AND CURSES_LIBRARY
    AND CDK_LIBRARY
)
    find_path(LIBCKD_INCLUDE_DIR cdk.h PATH_SUFFIXES cdk)

    set(TEMP ${CMAKE_REQUIRED_LIBRARIES})
    set(TEMP1 ${CMAKE_REQUIRED_INCLUDES})
    list(APPEND CMAKE_REQUIRED_LIBRARIES ${CURSES_LIBRARY} ${CDK_LIBRARY})
    list(APPEND CMAKE_REQUIRED_INCLUDES ${LIBCKD_INCLUDE_DIR})
    set(TMP_FLAGS "${CMAKE_C_FLAGS}")
    set(CMAKE_C_FLAGS "")
    check_symbol_exists(CDK_CSTRING2 "cdk.h" HAS_CDK_CSTRING2)
    set(CMAKE_C_FLAGS "${TMP_FLAGS}")
    set(CMAKE_REQUIRED_LIBRARIES ${TEMP})
    set(CMAKE_REQUIRED_INCLUDES ${TEMP1})
endif()

if(HAS_CDK_CSTRING2)
    include_directories(${LIBCKD_INCLUDE_DIR})
    project(iio-monitor C)
    add_executable(iio-monitor iio-monitor.c)
    target_link_libraries(
        iio-monitor iio ${PTHREAD_LIBRARIES} ${CURSES_LIBRARY} ${CDK_LIBRARY}
    )
    set(IIO_TESTS_TARGETS ${IIO_TESTS_TARGETS} iio-monitor)
else()
    message(STATUS "Curses Development Kit (CDK) missing or too old, skipping iio-monitor")
endif ()

set_target_properties(
    ${IIO_TESTS_TARGETS} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON
                                    C_EXTENSIONS OFF
)

# We don't install any of these, they are examples, not tools. We build them to
# make sure they build/work, not to deploy them.
