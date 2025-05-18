# SPDX-License-Identifier: MIT
#
# Copyright 2025, Nuno Sá <nuno.sa@analog.com>
# Copyright 2025, Robin Getz <rgetz503@gmail.com>
#
# You may use, modify, and distribute this script under the terms of the MIT License.
# See the LICENSE file or https://opensource.org/licenses/MIT for details.

function(check_pthread_set_name HAS_PTHREAD)
        include(CheckSymbolExists)
        set(CMAKE_REQUIRED_LIBRARIES ${PTHREAD_LIBRARIES})
        set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
        set(TMP_FLAGS "${CMAKE_C_FLAGS}")
        set(CMAKE_C_FLAGS "")
        check_symbol_exists(pthread_setname_np "pthread.h" ${HAS_PTHREAD})
        set(CMAKE_C_FLAGS "${TMP_FLAGS}")
        set(CMAKE_REQUIRED_LIBRARIES)
        set(CMAKE_REQUIRED_DEFINITIONS)
endfunction()

# This provides a CMake function to check for SPDX-License-Identifier
# headers in the source files associated with a specified CMake target.
#
function(spdx_check_target target)
    if(NOT TARGET ${target})
        message(WARNING "SPDX Check: Target '${target}' not found")
        return()
    endif()

    # Skip INTERFACE libraries (no sources)
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif()

    # Recursive function to collect sources
    function(collect_sources tgt out_list)
        get_target_property(tgt_type ${tgt} TYPE)
        if(tgt_type STREQUAL "INTERFACE_LIBRARY")
            set(${out_list} "" PARENT_SCOPE)
            return()
        endif()

        get_target_property(tgt_sources ${tgt} SOURCES)
        get_target_property(tgt_source_dir ${tgt} SOURCE_DIR)

        set(acc_sources "")
        foreach(src IN LISTS tgt_sources)
            # Resolve full path from target's directory
            if(IS_ABSOLUTE "${src}")
                set(full_path "${src}")
            else()
                set(full_path "${tgt_source_dir}/${src}")
            endif()
            list(APPEND acc_sources "${full_path}")
        endforeach()

        # Recursively check linked targets
        get_target_property(linked_libs ${tgt} LINK_LIBRARIES)
        if(linked_libs)
            foreach(lib IN LISTS linked_libs)
                if(TARGET ${lib})
                    collect_sources(${lib} dep_sources)
                    list(APPEND acc_sources ${dep_sources})
                endif()
            endforeach()
        endif()

        list(REMOVE_DUPLICATES acc_sources)
        set(${out_list} "${acc_sources}" PARENT_SCOPE)
    endfunction()

    collect_sources(${target} all_sources)
    #message(STATUS "source : ${all_sources}")

    # Normalize source dir for filtering and relative path printing
    get_filename_component(abs_source_dir "${CMAKE_SOURCE_DIR}" ABSOLUTE)

    unset(missing_spdx)
    unset(spdx_map)
    unset(spdx_licenses)
    get_filename_component(abs_binary_dir "${CMAKE_BINARY_DIR}" ABSOLUTE)

    foreach(abs_source_file IN LISTS all_sources)
        # Skip non-existent files quietly
        if(NOT EXISTS "${abs_source_file}")
            continue()
        endif()

	string(FIND "${abs_source_file}" "${abs_binary_dir}" build_pos)
	if(NOT build_pos EQUAL -1)
            continue()
        endif()

        # Skip files outside the source directory (e.g. build/generated)
        string(FIND "${abs_source_file}" "${abs_source_dir}" pos)
        if(NOT pos EQUAL 0)
            continue()
        endif()

        # Read content and extract license
        file(READ "${abs_source_file}" file_content)
        string(FIND "${file_content}" "SPDX-License-Identifier:" license_pos)

        file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}" "${abs_source_file}")

        if(license_pos EQUAL -1)
            list(APPEND missing_spdx "${rel_path}")
        else()
            string(SUBSTRING "${file_content}" ${license_pos} 100 license_segment)
            string(REGEX MATCH "SPDX-License-Identifier:[ \t]*([^ \t\r\n]+)" _match "${license_segment}")

            if(CMAKE_MATCH_COUNT GREATER 0)
                set(license "${CMAKE_MATCH_1}")
                # Group by license
                if(NOT DEFINED spdx_map_${license})
                    set(spdx_map_${license} "${rel_path}")
                else()
                    set(spdx_map_${license} "${spdx_map_${license}};${rel_path}")
                endif()

                list(FIND spdx_licenses "${license}" _idx)
                if(_idx EQUAL -1)
                    list(APPEND spdx_licenses "${license}")
                endif()
            else()
                list(APPEND missing_spdx "${rel_path}")
            endif()
        endif()
    endforeach()

    get_target_property(linked_libs ${target} LINK_LIBRARIES)

    set(external_libs "")
    foreach(lib IN LISTS linked_libs)
        if(NOT TARGET ${lib})
            list(APPEND external_libs "${lib}")
        endif()
    endforeach()

    if(NOT spdx_licenses AND NOT missing_spdx AND NOT external_libs)
    #	message(STATUS "skipping ${target}")
        return()
   endif()

    get_target_property(target_source_dir ${target} SOURCE_DIR)
    file(RELATIVE_PATH relative_target_dir "${CMAKE_SOURCE_DIR}" "${target_source_dir}")
    message(STATUS "=== License Check for ${relative_target_dir}: ${target} ==========")

    # Print licenses and associated files
    foreach(license IN LISTS spdx_licenses)
        string(REPLACE ";" ", " files "${spdx_map_${license}}")
        message(STATUS "    ${license}: ${files}")
    endforeach()

    # Print missing SPDX files
    if(missing_spdx)
        string(REPLACE ";" ", " missing_files "${missing_spdx}")
        message(STATUS "    Missing SPDX: ${missing_files}")
    endif()

    if(external_libs)
        string(REPLACE ";" ", " external_libs_str "${external_libs}")
        message(STATUS "    External libraries: ${external_libs_str}")
     endif()

    #message(STATUS "========== SPDX Check END: ${target} ==========")
endfunction()

# Recursive function to collect all targets in a directory and its subdirectories
function(spdx_check_all_targets root_dir)
    # Recursive helper
    function(_spdx_check_in_dir dir)
        # Get all build system targets in this directory
        get_property(local_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
        if(local_targets)
            foreach(target IN LISTS local_targets)
                spdx_check_target(${target})
            endforeach()
        endif()

        # Recurse into subdirectories
        get_property(subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
        foreach(subdir IN LISTS subdirs)
            _spdx_check_in_dir(${subdir})
        endforeach()
    endfunction()

    _spdx_check_in_dir(${root_dir})
endfunction()
