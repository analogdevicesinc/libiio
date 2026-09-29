cmake_minimum_required(VERSION 3.13)
project(libiio-sharp NONE)

if (WIN32)
    set(MCS_EXECUTABLE_NAME csc)
else ()
    set(MCS_EXECUTABLE_NAME mcs)
endif ()

find_program(
    MCS_EXECUTABLE
    NAMES ${MCS_EXECUTABLE_NAME}
    HINTS "C:/Windows/Microsoft.NET/Framework/v4.0.30319"
    PATHS ENV MCS_EXECUTABLE_PATH
    DOC "C# compiler")
mark_as_advanced(MCS_EXECUTABLE)

# NOTE: this only finds dotnet; whether it's actually used depends on the
# `if (DOTNET_EXECUTABLE)` block further down, which builds the .NET
# bindings variant independently of whether mcs/csc is also available.
find_program(
    DOTNET_EXECUTABLE
    NAMES dotnet
    DOC ".NET SDK (optional, for the .NET 5+ build)")
mark_as_advanced(DOTNET_EXECUTABLE)

set(CSHARP_DOTNET_TFM
    ""
    CACHE
        STRING
        "Target framework for the .NET build, e.g. net8.0 (default: newest the installed SDK supports)"
)

# SDK N builds up to netN.0, so target that unless pinned. Anything older
# than SDK 5, a runtime-only install, or an SDK that can't start MSBuild
# (e.g. SDK 5 without OpenSSL 1.1) can't build the bindings, so skip the
# .NET variant like when not found.
if (DOTNET_EXECUTABLE)
    execute_process(
        COMMAND ${DOTNET_EXECUTABLE} --version
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE DOTNET_SDK_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE DOTNET_SDK_RESULT
        ERROR_QUIET)
    execute_process(
        COMMAND ${DOTNET_EXECUTABLE} msbuild -version -nologo
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE DOTNET_MSBUILD_RESULT
        OUTPUT_QUIET ERROR_QUIET)

    if (DOTNET_SDK_RESULT EQUAL 0
        AND DOTNET_MSBUILD_RESULT EQUAL 0
        AND DOTNET_SDK_VERSION MATCHES "^([0-9]+)\\.")
        set(DOTNET_SDK_MAJOR ${CMAKE_MATCH_1})
    endif ()

    if (NOT DOTNET_SDK_MAJOR)
        message(STATUS "No usable .NET SDK found: skipping the .NET build")
        set(DOTNET_EXECUTABLE "")
    elseif (DOTNET_SDK_MAJOR LESS 5)
        message(
            STATUS
                ".NET SDK ${DOTNET_SDK_VERSION} is older than 5.0: skipping the .NET build"
        )
        set(DOTNET_EXECUTABLE "")
    elseif (CSHARP_DOTNET_TFM)
        set(LIBIIO_CS_NET_TFM ${CSHARP_DOTNET_TFM})
    else ()
        set(LIBIIO_CS_NET_TFM net${DOTNET_SDK_MAJOR}.0)
    endif ()
endif ()

if (NOT MCS_EXECUTABLE AND NOT DOTNET_EXECUTABLE)
    message(
        FATAL_ERROR
            "Neither a C# compiler (mcs/csc) nor the .NET SDK (dotnet) was found: cannot build C# bindings"
    )
endif ()

set(LIBIIO_CS_VERSION ${VERSION}.0)
set(LIBIIO_CS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Attr.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/Block.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/Channel.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/ChannelsMask.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/Context.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/Device.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/EventStream.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/IOBuffer.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/IIOEvent.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/Trigger.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/IioLib.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/NativeLibraryResolver.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/Scan.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/Stream.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/UTF8Marshaler.cs)

# Legacy Mono / .NET Framework build (mcs/csc). Also builds the C# tests,
# which reference the mcs-built libiio-sharp.dll directly and are not (yet)
# ported to dotnet.
if (MCS_EXECUTABLE)
    message(STATUS "Found C#: Building bindings")
    set(LIBIIO_CS_PC_IN "${CMAKE_CURRENT_SOURCE_DIR}/libiio-sharp.pc.cmakein")
    set(LIBIIO_CS_PC "${CMAKE_CURRENT_BINARY_DIR}/libiio-sharp-${VERSION}.pc")
    configure_file(${LIBIIO_CS_PC_IN} ${LIBIIO_CS_PC} @ONLY)
    if (NOT SKIP_INSTALL_ALL)
        install(FILES ${LIBIIO_CS_PC}
                DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig")
    endif ()

    set(LIBIIO_CS_DLL_CONFIG_IN
        "${CMAKE_CURRENT_SOURCE_DIR}/libiio-sharp.dll.config.cmakein")
    set(LIBIIO_CS_DLL_CONFIG
        "${CMAKE_CURRENT_BINARY_DIR}/libiio-sharp.dll.config")
    configure_file(${LIBIIO_CS_DLL_CONFIG_IN} ${LIBIIO_CS_DLL_CONFIG} @ONLY)
    if (NOT SKIP_INSTALL_ALL)
        install(
            FILES ${LIBIIO_CS_DLL_CONFIG}
            DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cli/libiio-sharp-${VERSION})
    endif ()

    set(LIBIIO_CS_INFO_IN ${CMAKE_CURRENT_SOURCE_DIR}/AssemblyInfo.cs.in)
    set(LIBIIO_CS_INFO ${CMAKE_CURRENT_BINARY_DIR}/AssemblyInfo.cs)
    configure_file(${LIBIIO_CS_INFO_IN} ${LIBIIO_CS_INFO} @ONLY)

    set(LIBIIO_CS_DLL "${CMAKE_CURRENT_BINARY_DIR}/libiio-sharp.dll")
    set(LIBIIO_CS_MCS_SOURCES ${LIBIIO_CS_SOURCES} ${LIBIIO_CS_INFO})

    foreach (SRC ${LIBIIO_CS_MCS_SOURCES})
        file(TO_NATIVE_PATH ${SRC} TMP)
        set(LIBIIO_CS_SOURCES_REALPATH ${LIBIIO_CS_SOURCES_REALPATH} ${TMP})
    endforeach (SRC)

    file(TO_NATIVE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/key.snk SIGN_KEY)
    file(TO_NATIVE_PATH ${LIBIIO_CS_DLL} LIBIIO_CS_DLL_OUT)

    add_custom_command(
        OUTPUT ${LIBIIO_CS_DLL}
        COMMAND
            ${MCS_EXECUTABLE} /target:library /langversion:5
            /out:${LIBIIO_CS_DLL_OUT} /debug /keyfile:${SIGN_KEY}
            ${LIBIIO_CS_SOURCES_REALPATH}
        DEPENDS ${LIBIIO_CS_MCS_SOURCES})

    add_custom_target(libiio-sharp ALL DEPENDS ${LIBIIO_CS_DLL})

    if (NOT SKIP_INSTALL_ALL)
        set(DEBUG_SYMBOLS_FILE "")
        if (MCS_EXECUTABLE_NAME STREQUAL "mcs")
            set(DEBUG_SYMBOLS_FILE "${LIBIIO_CS_DLL}.mdb")
        elseif (MCS_EXECUTABLE_NAME STREQUAL "csc")
            set(DEBUG_SYMBOLS_FILE
                "${CMAKE_CURRENT_BINARY_DIR}/libiio-sharp.pdb")
        endif ()
        install(
            FILES ${LIBIIO_CS_DLL} ${DEBUG_SYMBOLS_FILE}
            DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cli/libiio-sharp-${VERSION})
    endif ()

    add_subdirectory(tests)
else ()
    message(
        STATUS
            "mcs/csc not found: skipping legacy Mono/.NET Framework build (and C# tests, which depend on it)"
    )
endif ()

# .NET build (dotnet), independent of whether mcs/csc is available.
if (DOTNET_EXECUTABLE)
    message(
        STATUS
            "Found .NET SDK ${DOTNET_SDK_VERSION}: Building the ${LIBIIO_CS_NET_TFM} bindings variant"
    )
    set(LIBIIO_CS_NET_OUT_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/${LIBIIO_CS_NET_TFM}")
    set(LIBIIO_CS_NET_DLL "${LIBIIO_CS_NET_OUT_DIR}/libiio-sharp.dll")

    add_custom_command(
        OUTPUT ${LIBIIO_CS_NET_DLL}
        COMMAND
            ${DOTNET_EXECUTABLE} build
            ${CMAKE_CURRENT_SOURCE_DIR}/libiio-sharp-net.csproj -c Release
            -o ${LIBIIO_CS_NET_OUT_DIR} -p:Version=${LIBIIO_CS_VERSION}
            -p:LibiioSharpTfm=${LIBIIO_CS_NET_TFM}
        DEPENDS ${LIBIIO_CS_SOURCES}
                ${CMAKE_CURRENT_SOURCE_DIR}/libiio-sharp-net.csproj
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_custom_target(libiio-sharp-dotnet ALL DEPENDS ${LIBIIO_CS_NET_DLL})

    if (NOT SKIP_INSTALL_ALL)
        install(
            FILES ${LIBIIO_CS_NET_DLL}
            DESTINATION
                ${CMAKE_INSTALL_PREFIX}/lib/cli/libiio-sharp-${VERSION}/${LIBIIO_CS_NET_TFM}
        )
    endif ()
else ()
    message(STATUS ".NET SDK not found: skipping the .NET bindings variant")
endif ()

add_subdirectory(examples)
