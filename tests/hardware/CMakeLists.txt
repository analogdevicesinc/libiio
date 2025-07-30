set(TEST_SOURCES
  test_ad9364.c
)

foreach(TEST_SRC ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)

    add_executable(${TEST_NAME} ${TEST_SRC})

    target_link_libraries(${TEST_NAME} LINK_PRIVATE iio)

    add_test(
        NAME ${TEST_NAME}
        COMMAND ${TEST_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    if (WITH_GCOV)
      enable_coverage(${TEST_NAME})
    endif()
endforeach()
