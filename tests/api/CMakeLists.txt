enable_testing()

# API test targets
set(API_TEST_TARGETS
    test_error_handling
    test_scan
    test_toplevel
    test_context
    test_device
    test_events
    test_hwmon
    test_channel
    test_attr
    test_typed_attr
    test_buffer
    test_lowlevel
)

# Create a helper library for common test utilities
add_library(test_common STATIC test_framework.h)
set_target_properties(test_common PROPERTIES LINKER_LANGUAGE C)
target_include_directories(test_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_common PUBLIC iio iio-compat)

# Build each API test
foreach(test ${API_TEST_TARGETS})
    add_executable(${test} ${test}.c)
    target_link_libraries(${test} PRIVATE test_common iio)
    target_include_directories(${test} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
    )
    set_target_properties(${test} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )

    # Add as CTest
    add_test(NAME ${test} COMMAND ${test})
    if (WITH_GCOV)
        enable_coverage(${test})
    endif()
endforeach()

if(WITH_VITA49_2_BACKEND)
    add_executable(test_vita49_2_packets vita49_2/static/test_vita49_2_packets.c)
    target_link_libraries(test_vita49_2_packets PRIVATE test_common iio vita49_2)
    target_include_directories(test_vita49_2_packets PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
    )
    set_target_properties(test_vita49_2_packets PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    add_test(NAME test_vita49_2_packets COMMAND test_vita49_2_packets)

    add_executable(simulator vita49_2/simulator.c)
    set_target_properties(simulator PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(simulator PRIVATE vita49_2 iio)
endif()

# Master test runner
add_executable(run_all_api_tests run_all_api_tests.c)
target_link_libraries(run_all_api_tests PRIVATE test_common iio)
target_include_directories(run_all_api_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
set_target_properties(run_all_api_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# Make targets available to parent scope
set(API_TEST_TARGETS ${API_TEST_TARGETS} PARENT_SCOPE)
