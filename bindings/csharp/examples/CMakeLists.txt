cmake_minimum_required(VERSION 3.13)

message(STATUS "Configuring C# examples")

set(CSHARP_EXAMPLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/csharp-example-bin")

# Legacy Mono / .NET Framework examples (mcs/csc), built against the
# mcs-built libiio-sharp.dll.
if (MCS_EXECUTABLE)
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
    function (add_csharp_example_target)
        cmake_parse_arguments(ARG "" "TARGET_NAME;EXE_NAME;COMMENT" "SOURCES"
                              ${ARGN})

        set(EXE_PATH "${CSHARP_EXAMPLE_DIR}/${ARG_EXE_NAME}")

        set(SOURCES_NATIVE "")
        foreach (SRC ${ARG_SOURCES})
            file(TO_NATIVE_PATH ${SRC} TMP)
            list(APPEND SOURCES_NATIVE ${TMP})
        endforeach ()

        file(TO_NATIVE_PATH ${EXE_PATH} EXE_PATH_NATIVE)

        add_custom_command(
            OUTPUT ${EXE_PATH}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CSHARP_EXAMPLE_DIR}
            COMMAND
                ${MCS_EXECUTABLE} /target:exe /langversion:5
                /out:${EXE_PATH_NATIVE} /reference:${LIBIIO_SHARP_REF_NATIVE}
                ${SOURCES_NATIVE}
            DEPENDS ${ARG_SOURCES} libiio-sharp
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "${ARG_COMMENT}")

        add_custom_target(${ARG_TARGET_NAME} ALL DEPENDS ${EXE_PATH})
    endfunction ()

    add_csharp_example_target(
        TARGET_NAME
        libiio-csharp-example-program
        EXE_NAME
        ExampleProgram.exe
        SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/ExampleProgram.cs
        COMMENT
        "Building C# ExampleProgram")

    add_csharp_example_target(
        TARGET_NAME
        libiio-csharp-example-pluto
        EXE_NAME
        PlutoExample.exe
        SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/PlutoExample.cs
        COMMENT
        "Building C# PlutoExample")

    add_csharp_example_target(
        TARGET_NAME
        libiio-csharp-example-event
        EXE_NAME
        ExampleIIOEvent.exe
        SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/ExampleIIOEvent.cs
        COMMENT
        "Building C# ExampleIIOEvent")

    add_custom_target(
        libiio-csharp-examples ALL
        DEPENDS libiio-csharp-example-program libiio-csharp-example-pluto
                libiio-csharp-example-event)

    message(STATUS "C# examples configured in: ${CSHARP_EXAMPLE_DIR}")
else ()
    message(
        STATUS
            "mcs/csc not found: skipping legacy Mono/.NET Framework C# examples"
    )
endif ()

# .NET 5+ examples (dotnet), independent of whether mcs/csc is available.
# Each example needs its own csproj (a single project can't have three
# `Main` methods), referencing the already-built libiio-sharp.dll like the
# mcs examples do, so the bindings aren't rebuilt by every example.
if (DOTNET_EXECUTABLE)
    set(CSHARP_EXAMPLE_NET_DIR "${CSHARP_EXAMPLE_DIR}/net5.0")

    # Builds a .NET 5+ C# example executable and creates a custom target
    # for it.
    #
    # add_csharp_example_net_target(
    #     TARGET_NAME   <name>        # custom target name
    #     PROJECT       <name.csproj> # csproj file, relative to this dir
    #     SOURCES       <file> ...    # source .cs file(s) the csproj compiles
    # )
    function (add_csharp_example_net_target)
        cmake_parse_arguments(ARG "" "TARGET_NAME;PROJECT" "SOURCES" ${ARGN})

        get_filename_component(EXE_NAME ${ARG_PROJECT} NAME_WE)
        set(EXE_PATH "${CSHARP_EXAMPLE_NET_DIR}/${EXE_NAME}.dll")

        add_custom_command(
            OUTPUT ${EXE_PATH}
            COMMAND
                ${DOTNET_EXECUTABLE} build
                ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_PROJECT} -c Release
                -o ${CSHARP_EXAMPLE_NET_DIR} -p:Version=${LIBIIO_CS_VERSION}
                -p:LibiioSharpDll=${LIBIIO_CS_NET_DLL}
            DEPENDS ${ARG_SOURCES} ${LIBIIO_CS_SOURCES}
                    ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_PROJECT}
                    ${CMAKE_CURRENT_SOURCE_DIR}/../libiio-sharp-net.csproj
                    libiio-sharp-net5
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Building .NET 5+ C# example: ${ARG_PROJECT}")

        add_custom_target(${ARG_TARGET_NAME} ALL DEPENDS ${EXE_PATH})
    endfunction ()

    add_csharp_example_net_target(
        TARGET_NAME
        libiio-csharp-example-program-net5
        PROJECT
        ExampleProgram.csproj
        SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/ExampleProgram.cs)

    add_csharp_example_net_target(
        TARGET_NAME
        libiio-csharp-example-pluto-net5
        PROJECT
        PlutoExample.csproj
        SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/PlutoExample.cs)

    add_csharp_example_net_target(
        TARGET_NAME
        libiio-csharp-example-event-net5
        PROJECT
        ExampleIIOEvent.csproj
        SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/ExampleIIOEvent.cs)

    add_custom_target(
        libiio-csharp-examples-net5 ALL
        DEPENDS libiio-csharp-example-program-net5
                libiio-csharp-example-pluto-net5
                libiio-csharp-example-event-net5)

    message(
        STATUS "C# .NET 5+ examples configured in: ${CSHARP_EXAMPLE_NET_DIR}")
else ()
    message(
        STATUS ".NET SDK not found: skipping the .NET 5+ C# examples variant")
endif ()
