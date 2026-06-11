cmake_minimum_required(VERSION 3.13)

message(STATUS "Configuring C# examples")

set(CSHARP_EXAMPLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/csharp-example-bin")
set(LIBIIO_SHARP_REF "${CMAKE_BINARY_DIR}/bindings/csharp/libiio-sharp.dll")

file(TO_NATIVE_PATH ${LIBIIO_SHARP_REF} LIBIIO_SHARP_REF_NATIVE)

# Builds a C# example executable and creates a custom target for it.
#
# add_csharp_example_target(
#     TARGET_NAME   <name>        # custom target name
#     EXE_NAME      <name.exe>    # output executable filename
#     SOURCES       <file> ...    # source .cs files
#     COMMENT       <string>      # build comment shown by make
# )
function(add_csharp_example_target)
    cmake_parse_arguments(ARG "" "TARGET_NAME;EXE_NAME;COMMENT" "SOURCES" ${ARGN})

    set(EXE_PATH "${CSHARP_EXAMPLE_DIR}/${ARG_EXE_NAME}")

    set(SOURCES_NATIVE "")
    foreach(SRC ${ARG_SOURCES})
        file(TO_NATIVE_PATH ${SRC} TMP)
        list(APPEND SOURCES_NATIVE ${TMP})
    endforeach()

    file(TO_NATIVE_PATH ${EXE_PATH} EXE_PATH_NATIVE)

    add_custom_command(OUTPUT ${EXE_PATH}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CSHARP_EXAMPLE_DIR}
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

add_csharp_example_target(
    TARGET_NAME libiio-csharp-example-program
    EXE_NAME    ExampleProgram.exe
    SOURCES     ${CMAKE_CURRENT_SOURCE_DIR}/ExampleProgram.cs
    COMMENT "Building C# ExampleProgram"
)

add_csharp_example_target(
    TARGET_NAME libiio-csharp-example-pluto
    EXE_NAME    PlutoExample.exe
    SOURCES     ${CMAKE_CURRENT_SOURCE_DIR}/PlutoExample.cs
    COMMENT "Building C# PlutoExample"
)

add_csharp_example_target(
    TARGET_NAME libiio-csharp-example-event
    EXE_NAME    ExampleIIOEvent.exe
    SOURCES     ${CMAKE_CURRENT_SOURCE_DIR}/ExampleIIOEvent.cs
    COMMENT "Building C# ExampleIIOEvent"
)

add_custom_target(libiio-csharp-examples ALL
    DEPENDS libiio-csharp-example-program libiio-csharp-example-pluto libiio-csharp-example-event
)

message(STATUS "C# examples configured in: ${CSHARP_EXAMPLE_DIR}")
