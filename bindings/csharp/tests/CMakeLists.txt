cmake_minimum_required(VERSION 3.13)

message(STATUS "Configuring C# tests")

# Define output directory
set(CSHARP_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/csharp-test-bin")

# Reference the built libiio-sharp.dll
set(LIBIIO_SHARP_REF "${CMAKE_BINARY_DIR}/bindings/csharp/libiio-sharp.dll")

file(TO_NATIVE_PATH ${LIBIIO_SHARP_REF} LIBIIO_SHARP_REF_NATIVE)

# Builds a C# test executable and creates a custom target for it.
#
# add_csharp_test_target(
#     TARGET_NAME   <name>          # custom target name (e.g. libiio-csharp-smoke-tests)
#     EXE_NAME      <name.exe>      # output executable filename
#     SOURCES       <file> ...      # source .cs files (relative or absolute)
#     COMMENT       <string>        # build comment shown by make
#     [EXTRA_COMMANDS <cmd> ...]    # optional extra COMMAND lines inserted before mcs
# )
function(add_csharp_test_target)
    cmake_parse_arguments(ARG "" "TARGET_NAME;EXE_NAME;COMMENT" "SOURCES;EXTRA_COMMANDS" ${ARGN})

    set(EXE_PATH "${CSHARP_TEST_DIR}/${ARG_EXE_NAME}")

    set(SOURCES_NATIVE "")
    foreach(SRC ${ARG_SOURCES})
        file(TO_NATIVE_PATH ${SRC} TMP)
        list(APPEND SOURCES_NATIVE ${TMP})
    endforeach()

    file(TO_NATIVE_PATH ${EXE_PATH} EXE_PATH_NATIVE)

    add_custom_command(OUTPUT ${EXE_PATH}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CSHARP_TEST_DIR}
        ${ARG_EXTRA_COMMANDS}
        COMMAND ${MCS_EXECUTABLE}
            /target:exe
            /langversion:5
            /out:${EXE_PATH_NATIVE}
            /reference:${LIBIIO_SHARP_REF_NATIVE}
            ${SOURCES_NATIVE}
        DEPENDS ${ARG_SOURCES} libiio-sharp
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "${ARG_COMMENT}"
    )

    add_custom_target(${ARG_TARGET_NAME} ALL DEPENDS ${EXE_PATH})
endfunction()

# --- Smoke Tests ---

add_csharp_test_target(
    TARGET_NAME libiio-csharp-smoke-tests
    EXE_NAME    LibiioSmokeTests.exe
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/TestFramework.cs
        ${CMAKE_CURRENT_SOURCE_DIR}/SmokeTests.cs
    COMMENT "Building C# smoke tests"
)

# --- Integration Tests ---

set(XML_RESOURCES_DIR "${CMAKE_SOURCE_DIR}/tests/resources/xmls")

add_csharp_test_target(
    TARGET_NAME libiio-csharp-integration-tests
    EXE_NAME    LibiioIntegrationTests.exe
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/TestFramework.cs
        ${CMAKE_CURRENT_SOURCE_DIR}/IntegrationTests.cs
    EXTRA_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${XML_RESOURCES_DIR} ${CSHARP_TEST_DIR}
    COMMENT "Building C# integration tests"
)

# --- Combined target (builds both) ---
add_custom_target(libiio-csharp-tests ALL
    DEPENDS libiio-csharp-smoke-tests libiio-csharp-integration-tests
)

message(STATUS "C# tests configured in: ${CSHARP_TEST_DIR}")
