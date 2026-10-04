cmake_minimum_required(VERSION 3.13)

set(IIO_BENCHMARK_TARGETS
    bench_context
    bench_attr
    bench_block
    bench_buffer
    bench_streaming)

add_library(iio_bench_common OBJECT bench_common.c bench_measure.c)
target_link_libraries(iio_bench_common LINK_PRIVATE iio)
set_target_properties(
    iio_bench_common
    PROPERTIES C_STANDARD 99
               C_STANDARD_REQUIRED ON
               C_EXTENSIONS OFF)

foreach (bench ${IIO_BENCHMARK_TARGETS})
    add_executable(${bench} ${bench}.c)
    set_target_properties(
        ${bench}
        PROPERTIES C_STANDARD 99
                   C_STANDARD_REQUIRED ON
                   C_EXTENSIONS OFF)
    target_link_libraries(${bench} LINK_PRIVATE iio iio_bench_common m)
endforeach ()

# --- Optional v0-API-level benchmarks (bench_v0_context/bench_v0_attr) ---
#
# v0 and v1 have incompatible headers/ABIs, so these can't link the v1 iio
# target above; they need a real v0 libiio found on disk. LIBIIO_V0_ROOT
# points at a directory containing that build (its iio.h and libiio.so*);
# if unset, a couple of conventional worktree locations are auto-probed so
# an existing "git worktree add ../libiio-v0-worktree libiio-v0" checkout is
# picked up with zero flags. Either way, presence is confirmed by actually
# probing the header for v1-only API (struct iio_attr/iio_attr_read_raw),
# not by trusting the path name - a "v0" directory that happens to hold a v1
# build should not silently build bogus "v0" benchmarks.
set(LIBIIO_V0_ROOT "" CACHE PATH
    "Root directory of a v0 libiio build (containing iio.h/libiio.so*), for bench_v0_context/bench_v0_attr")

set(_bench_v0_candidates "")
if (LIBIIO_V0_ROOT)
    list(APPEND _bench_v0_candidates "${LIBIIO_V0_ROOT}")
else ()
    list(APPEND _bench_v0_candidates
        "${CMAKE_CURRENT_SOURCE_DIR}/../../libiio-v0-worktree"
        "${CMAKE_CURRENT_SOURCE_DIR}/../libiio-v0-worktree")
endif ()

set(_bench_v0_found FALSE)

foreach (_candidate ${_bench_v0_candidates})
    if (_bench_v0_found)
        break()
    endif ()

    find_path(BENCH_V0_INCLUDE_DIR iio.h
              PATHS "${_candidate}"
              NO_DEFAULT_PATH)

    # The library isn't necessarily next to iio.h - a source-tree worktree
    # like "git worktree add ../libiio-v0-worktree libiio-v0" keeps iio.h at
    # its root but the built libiio.so under whatever out-of-source build
    # directory was used (e.g. build_v0tools/). Glob for it instead of
    # guessing fixed subdirectory names.
    file(GLOB_RECURSE _bench_v0_so_matches "${_candidate}/*libiio.so")
    if (_bench_v0_so_matches)
        list(GET _bench_v0_so_matches 0 BENCH_V0_LIBRARY)
        set(BENCH_V0_LIBRARY "${BENCH_V0_LIBRARY}" CACHE FILEPATH "v0 libiio library" FORCE)
    endif ()

    if (BENCH_V0_INCLUDE_DIR AND BENCH_V0_LIBRARY)
        # A bare "struct iio_attr *p;" would compile even against v0's
        # header - C lets you forward-declare an incomplete struct tag
        # inline, so that alone proves nothing. iio_attr_read_raw is a
        # real function only v1 declares; check_symbol_exists fails with a
        # genuine "undeclared identifier" error if it isn't declared
        # anywhere, unlike calling an unknown function (which C only warns
        # about via implicit declaration).
        include(CheckSymbolExists)
        set(CMAKE_REQUIRED_INCLUDES "${BENCH_V0_INCLUDE_DIR}")
        check_symbol_exists(iio_attr_read_raw "iio.h" BENCH_V0_LOOKS_LIKE_V1)
        unset(CMAKE_REQUIRED_INCLUDES)

        if (BENCH_V0_LOOKS_LIKE_V1)
            message(STATUS "benchmarks: ${_candidate} looks like a v1 libiio, skipping v0 benchmarks there")
        else ()
            message(STATUS "benchmarks: found v0-shaped libiio at ${_candidate}, enabling bench_v0_context/bench_v0_attr")
            set(_bench_v0_found TRUE)
        endif ()
    endif ()

    unset(BENCH_V0_LOOKS_LIKE_V1 CACHE)
    if (NOT _bench_v0_found)
        unset(BENCH_V0_INCLUDE_DIR CACHE)
        unset(BENCH_V0_LIBRARY CACHE)
    endif ()
endforeach ()

if (_bench_v0_found)
    get_filename_component(BENCH_V0_LIBDIR "${BENCH_V0_LIBRARY}" DIRECTORY)

    add_library(iio_bench_v0_measure OBJECT bench_measure.c)
    set_target_properties(
        iio_bench_v0_measure
        PROPERTIES C_STANDARD 99
                   C_STANDARD_REQUIRED ON
                   C_EXTENSIONS OFF)
    # bench_measure.c uses clock_gettime()/popen()/gethostname()/gmtime_r()
    # (POSIX.1-2008), and doesn't link the v1 "iio" target here to inherit
    # its _POSIX_C_SOURCE compile definition - set it directly instead,
    # matching the top-level CMakeLists.txt's IIO_POSIX_C_SOURCE.
    target_compile_definitions(iio_bench_v0_measure PRIVATE _POSIX_C_SOURCE=200809L)

    foreach (bench bench_v0_context bench_v0_attr)
        add_executable(${bench} ${bench}.c)
        target_include_directories(${bench} PRIVATE "${BENCH_V0_INCLUDE_DIR}")
        target_link_libraries(${bench} LINK_PRIVATE "${BENCH_V0_LIBRARY}" iio_bench_v0_measure m)
        set_target_properties(
            ${bench}
            PROPERTIES C_STANDARD 99
                       C_STANDARD_REQUIRED ON
                       C_EXTENSIONS OFF
                       BUILD_RPATH "${BENCH_V0_LIBDIR}"
                       INSTALL_RPATH "${BENCH_V0_LIBDIR}")
    endforeach ()
else ()
    message(STATUS "benchmarks: no v0-shaped libiio found (set -DLIBIIO_V0_ROOT=<path> to point at one), skipping bench_v0_context/bench_v0_attr")
endif ()
