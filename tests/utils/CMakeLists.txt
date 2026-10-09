# The XML backend supplies metadata; the test executable mocks only attribute writes.
if (WITH_UTILS AND WITH_XML_BACKEND)
    find_package(Python3 COMPONENTS Interpreter)
    if (Python3_Interpreter_FOUND)
        add_executable(test_iio_attr_cli test_iio_attr.c)
        target_link_libraries(test_iio_attr_cli PRIVATE iio iio_utils_helper)
        target_include_directories(test_iio_attr_cli
                                   PRIVATE ${CMAKE_SOURCE_DIR}/utils)
        set_target_properties(
            test_iio_attr_cli
            PROPERTIES C_STANDARD 99
                       C_STANDARD_REQUIRED ON
                       C_EXTENSIONS OFF)
        add_test(
            NAME iio_attr_write_integrity
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test_iio_attr.py
                $<TARGET_FILE:test_iio_attr_cli>)
        add_test(
            NAME iio_attr_write_integrity_optimized
            COMMAND
                ${Python3_EXECUTABLE} -O
                ${CMAKE_CURRENT_SOURCE_DIR}/test_iio_attr.py
                $<TARGET_FILE:test_iio_attr_cli>)
        set_tests_properties(
            iio_attr_write_integrity iio_attr_write_integrity_optimized
            PROPERTIES LABELS "utils")
    endif ()
endif ()
