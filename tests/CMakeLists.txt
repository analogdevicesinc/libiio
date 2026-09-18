add_definitions(-UNDEBUG)
if (TESTS_DEBUG)
    add_definitions(-DTESTS_DEBUG)
endif ()
add_subdirectory(hardware)
add_subdirectory(api)

# Check that libiio.map still describes exactly the public API and that the
# built library exports nothing beyond it. Without this, adding an __api
# declaration without updating the map silently fails to export the symbol -
# which is how iio_register_get_ticks_us_cb went missing for 16 months.
if (HAS_LD_VERSION_SCRIPT AND LIBIIO_VERSION_SCRIPT)
    find_package(Python3 COMPONENTS Interpreter)

    # CMake only sets CMAKE_NM for some toolchains, but when it does it points
    # at the right one for the target, which matters when cross-compiling.
    if (CMAKE_NM)
        set(SYMBOL_MAP_NM "${CMAKE_NM}")
    else ()
        find_program(SYMBOL_MAP_NM nm)
    endif ()

    if (Python3_Interpreter_FOUND AND SYMBOL_MAP_NM)
        # The C++ binding header carries no __api declarations of its own.
        set(SYMBOL_MAP_HEADERS ${IIO_PUBLIC_HEADERS})
        list(FILTER SYMBOL_MAP_HEADERS INCLUDE REGEX "\\.h$")
        list(TRANSFORM SYMBOL_MAP_HEADERS PREPEND "${CMAKE_SOURCE_DIR}/")

        add_test(
            NAME symbol_map
            COMMAND
                ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/CI/symbol-map.py
                check ${SYMBOL_MAP_HEADERS} --map ${LIBIIO_VERSION_SCRIPT}
                --library $<TARGET_FILE:iio> --nm ${SYMBOL_MAP_NM})
    else ()
        message(STATUS "Python or nm not found; skipping the symbol map test")
    endif ()
endif ()
