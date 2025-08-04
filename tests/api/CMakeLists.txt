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
)

# Create a helper library for common test utilities
add_library(test_common STATIC test_framework.h)
set_target_properties(test_common PROPERTIES LINKER_LANGUAGE C)
target_include_directories(test_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_common PUBLIC iio)

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

# Make targets available to parent scope
set(API_TEST_TARGETS ${API_TEST_TARGETS} PARENT_SCOPE)
