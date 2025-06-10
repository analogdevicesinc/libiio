# SPDX-License-Identifier: MIT
#
# Copyright 2025, Nuno Sá <nuno.sa@analog.com>
# Copyright 2025, Robin Getz <rgetz503@gmail.com>
#
# You may use, modify, and distribute this script under the terms of the MIT License.
# See the LICENSE file or https://opensource.org/licenses/MIT for details.

# Avoid multiple duplicated function definitions
include_guard()

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

# Cache for include‐traces:  abs_c_path => semicolon‐list of headers
set_property(GLOBAL PROPERTY _LIB_PREPROCESS_CACHE "")

# Cache for licenses:         abs_file_path => single‐string license text
set_property(GLOBAL PROPERTY _LIB_LICENSE_CACHE "")

# Helper: turn a path into a valid CMake variable name
function(_file_to_var filepath out_var)
        # replace non‐alphanumeric with underscores
        string(REGEX REPLACE "[^A-Za-z0-9]" "_" v "${filepath}")
        set(${out_var} "IOLIB_HDRS_${v}" PARENT_SCOPE)
endfunction()

# helper: map a filename into a valid property name
function(_file_to_prop filepath out_prop)
        # e.g. "/home/.../foo.c" → "IOLIB_HDRS__home__foo_c"
        string(REGEX REPLACE "[^A-Za-z0-9]" "_" v "${filepath}")
        set(${out_prop} "IOLIB_HDRS_${v}" PARENT_SCOPE)
endfunction()

# Returns "" or the full semicolon list of headers
function(get_cached_headers abs_path out_var)
        _file_to_prop("${abs_path}" prop)
        # This gets the *value* of the GLOBAL property named ${prop}
        get_property(val GLOBAL PROPERTY "${prop}")
        # message(STATUS "lookup ${prop} for ${abs_path} = ${val}")
        if(val)
                # message(STATUS "found")
                set(${out_var} "${val}" PARENT_SCOPE)
        else()
                # message(STATUS "missing")
                set(${out_var} "" PARENT_SCOPE)
        endif()
endfunction()

# Store the semicolon list hdr_list for abs_path
function(set_cached_headers abs_path hdr_list)
        _file_to_prop("${abs_path}" prop)
        # store as a GLOBAL PROPERTY
        set_property(GLOBAL PROPERTY "${prop}" "${hdr_list}")
        # message(STATUS "Cached headers for ${abs_path}: ${hdr_list}")
endfunction()

function(get_cached_license abs_path out_var)
        get_property(_lc GLOBAL PROPERTY _LIB_LICENSE_CACHE)
        foreach(entry IN LISTS _lc)
                string(FIND "${entry}" "${abs_path}=" pos)
                if(pos EQUAL 0)
                        string(LENGTH "${abs_path}=" prefix_len)
                        string(SUBSTRING "${entry}" ${prefix_len} -1 lic)
                        set(${out_var} "${lic}" PARENT_SCOPE)
                        return()
                endif()
        endforeach()
        set(${out_var}
            ""
            PARENT_SCOPE)
endfunction()

# Usage: Collects all compile definitions and options for a target, including recursively from
# dependencies, and also includes directory/global properties at each step.
# collect_all_compile_defs_and_opts(my_target ALL_DEFS ALL_OPTS)

function(collect_all_compile_defs_and_opts tgt out_defs out_opts)
        get_target_property(tgt_source_dir ${tgt} SOURCE_DIR)
        if(NOT tgt_source_dir)
                # message(WARNING "Could not determine SOURCE_DIR for target ${tgt}")
                set(tgt_source_dir "${CMAKE_CURRENT_SOURCE_DIR}") # Fallback
        endif()
        set(all_defs "")
        set(all_opts "")
        set(_visited "")
        _get_recursive_interface_properties(${tgt} "${tgt_source_dir}" all_defs all_opts
                                            _visited)
        list(REMOVE_DUPLICATES all_defs)
        list(REMOVE_DUPLICATES all_opts)
        set(${out_defs} "${all_defs}" PARENT_SCOPE)
        set(${out_opts} "${all_opts}" PARENT_SCOPE)
endfunction()

function(_collect_direct_properties tgt tgt_source_dir defs_out opts_out)
        # Always collect INTERFACE properties
        get_target_property(interface_defs ${tgt} INTERFACE_COMPILE_DEFINITIONS)
        get_target_property(interface_comp_defs ${tgt} INTERFACE_COMPILE_OPTIONS)
        get_target_property(_target_type ${tgt} TYPE)

        set(private_defs "")
        set(compile_defs "")
        set(directory_defs "")
        set(global_defs "")

        if(NOT _target_type STREQUAL "INTERFACE_LIBRARY")
                get_target_property(private_defs ${tgt} COMPILE_DEFINITIONS)
                get_target_property(compile_defs ${tgt} COMPILE_OPTIONS)
                get_property(
                        directory_defs
                        DIRECTORY ${tgt_source_dir}
                        PROPERTY COMPILE_DEFINITIONS)
                get_property(global_defs GLOBAL PROPERTY COMPILE_DEFINITIONS)
        endif()

        # message(STATUS "Target: ${tgt}, TYPE: ${_target_type}")
        # message(STATUS "INTERFACE_COMPILE_DEFINITIONS: ${interface_defs}")
        # message(STATUS "INTERFACE_COMPILE_OPTIONS: ${interface_comp_defs}")
        # message(STATUS "  PRIVATE COMPILE_DEFINITIONS: ${private_defs}")
        # message(STATUS "  PRIVATE COMPILE_OPTIONS: ${compile_defs}")
        # message(STATUS "  DIRECTORY COMPILE_DEFINITIONS: ${directory_defs}")
        # message(STATUS "  GLOBAL COMPILE_DEFINITIONS: ${global_defs}")

        set(_defs "")
        foreach(def IN LISTS private_defs interface_defs directory_defs global_defs)
                if("${def}" MATCHES "\\$<[^>]*>")
                        continue()
                endif()
                if("${def}" MATCHES "^-")
                        continue()
                endif()
                if(NOT def OR "${def}" MATCHES "-NOTFOUND$")
                        continue()
                endif()
                list(APPEND _defs "${def}")
        endforeach()

        set(_opts "")
        foreach(opt IN LISTS compile_defs interface_comp_defs)
                if("${opt}" MATCHES "\\$<[^>]*>")
                        continue()
                endif()
                if(NOT opt OR "${opt}" MATCHES "-NOTFOUND$")
                        continue()
                endif()
                list(APPEND _opts "${opt}")
        endforeach()

        set(${defs_out}
            "${_defs}"
            PARENT_SCOPE)
        set(${opts_out}
            "${_opts}"
            PARENT_SCOPE)
endfunction()

function(_get_recursive_interface_properties tgt tgt_source_dir out_defs out_opts _visited)
        # message(STATUS "Visiting target: ${tgt}, source dir: ${tgt_source_dir}")
        if(";${_visited};" MATCHES ";${tgt};")
                # message(STATUS "  Already visited target: ${tgt}, skipping to avoid cycles")
                # Set outputs to parent so that empty value doesn't overwrite
                set(${out_defs}
                    ""
                    PARENT_SCOPE)
                set(${out_opts}
                    ""
                    PARENT_SCOPE)
                return()
        endif()
        list(APPEND _visited "${tgt}")

        # Collect direct properties for this target
        _collect_direct_properties(${tgt} "${tgt_source_dir}" direct_defs direct_opts)

        set(accum_defs "${direct_defs}")
        set(accum_opts "${direct_opts}")

        get_target_property(_target_type ${tgt} TYPE)
        if(_target_type STREQUAL "INTERFACE_LIBRARY")
                # message(STATUS "INTERFACE_LINK_LIBRARIES")
                get_target_property(_deps ${tgt} INTERFACE_LINK_LIBRARIES)
        else()
                # message(STATUS "LINK_LIBRARIES")
                get_target_property(_deps ${tgt} LINK_LIBRARIES)
        endif()
        if(NOT _deps OR _deps MATCHES "-NOTFOUND")
                # message(STATUS "No dependencies found for target: ${tgt}")
                # Set outputs in parent scope!
                set(${out_defs}
                    "${accum_defs}"
                    PARENT_SCOPE)
                set(${out_opts}
                    "${accum_opts}"
                    PARENT_SCOPE)
                return()
        endif()
        # message(STATUS "deps : ${_deps}")
        foreach(dep IN LISTS _deps)
                unset(dep_src_dir)
                if(TARGET ${dep})
                        get_target_property(interface_defs ${dep}
                                            INTERFACE_COMPILE_DEFINITIONS)
                        # message(STATUS "  INTERFACE_COMPILE_DEFINITIONS for dep ${dep}:
                        # ${interface_defs}")
                        get_target_property(_dep_type ${dep} TYPE)
                        if(NOT _dep_type STREQUAL "INTERFACE_LIBRARY")
                                get_target_property(dep_src_dir ${dep} SOURCE_DIR)
                        endif()
                        if(NOT dep_src_dir)
                                # message(STATUS "Could not determine SOURCE_DIR for target ${dep}, using fallback: ${tgt_source_dir}")
                                set(dep_src_dir "${tgt_source_dir}")
                        endif()
                        # message(STATUS "  Recursing into dependency: ${dep} (dep_src_dir: ${dep_src_dir})")
                        _get_recursive_interface_properties(${dep} "${dep_src_dir}" dep_defs
                                                            dep_opts "${_visited}")
                        list(APPEND accum_defs ${dep_defs})
                        list(APPEND accum_opts ${dep_opts})
                else()
                        # message(STATUS "skipping target ${dep}")
                endif()
        endforeach()

        # After recursion, set the outputs in parent scope
        set(${out_defs}
            "${accum_defs}"
            PARENT_SCOPE)
        set(${out_opts}
            "${accum_opts}"
            PARENT_SCOPE)
endfunction()

function(set_cached_license abs_path license_text)
        get_property(_lc GLOBAL PROPERTY _LIB_LICENSE_CACHE)
        list(FILTER _lc EXCLUDE REGEX "^${abs_path}=.*$")
        list(APPEND _lc "${abs_path}=${license_text}")
        set_property(GLOBAL PROPERTY _LIB_LICENSE_CACHE VALUE "${_lc}")
endfunction()

# -----------------------------------------------------------------------------
# collect_sources<tgt> - Recursively gather .c/.h used by <tgt> (including via #include) -
# Avoids re-preprocessing the same file more than once - Honors target’s include dirs and
# compile definitions - Skips system headers, only keeps ones under ${CMAKE_SOURCE_DIR}
# -----------------------------------------------------------------------------
function(collect_sources tgt out_list)
        # Get the list of source files for this target
        get_target_property(tgt_type ${tgt} TYPE)
        if(tgt_type STREQUAL "INTERFACE_LIBRARY")
                set(${out_list} "" PARENT_SCOPE)
                return()
        endif()

        # Get the raw list (may contain “-NOTFOUND” placeholders)
        get_target_property(tgt_sources ${tgt} SOURCES)
        # Remove any “-NOTFOUND” entries
        list(FILTER tgt_sources EXCLUDE REGEX "-NOTFOUND$")

        get_target_property(tgt_source_dir ${tgt} SOURCE_DIR)
        file(RELATIVE_PATH tgt_subdir "${CMAKE_SOURCE_DIR}" "${tgt_source_dir}")
        set(tgt_binary_dir "${CMAKE_BINARY_DIR}/${tgt_subdir}")

        get_target_property(target_cxx_std ${tgt} CXX_STANDARD)
        get_target_property(target_cxx_ext ${tgt} CXX_EXTENSIONS)
        get_target_property(target_c_std ${tgt} C_STANDARD)
        get_target_property(target_c_ext ${tgt} C_EXTENSIONS)

        if(NOT target_cxx_std OR target_cxx_std MATCHES "-NOTFOUND")
                unset(target_cxx_std)
        endif()
        if(NOT target_c_std OR target_c_std MATCHES "-NOTFOUND")
                unset(target_c_std)
        endif()

        if(target_cxx_std)
                if(MSVC)
                        set(std_flag "/std:c++${target_cxx_std}")
                elseif(target_cxx_ext)
                        set(std_flag "-std=gnu++${target_cxx_std}")
                else()
                        set(std_flag "-std=c++${target_cxx_std}")
                endif()
        elseif(target_c_std)
                # No way to do this for MSVC
                if(target_c_ext)
                        set(std_flag "-std=gnu${target_c_std}")
                else()
                        set(std_flag "-std=c${target_c_std}")
                endif()
        else()
                set(std_flag "")
        endif()
        # message(STATUS "setting std to ${std_flag}")

        # Prepare accumulators
        set(acc_sources "") # all .c files we see
        set(acc_headers "") # all .h files we include
        set(seen_headers "") # to dedupe headers

        if(APPLE)
                execute_process(
                        COMMAND xcrun --show-sdk-path
                        OUTPUT_VARIABLE macos_sdk_path
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
                set(sdk_flag "-isysroot" "${macos_sdk_path}")
        else()
                set(sdk_flag "")
        endif()

        # message(STATUS "looking for ${tgt} in these files : ${tgt_sources}")

        # Loop over each source file
        foreach(src IN LISTS tgt_sources)
                # Resolve full path from target's directory
                if(IS_ABSOLUTE "${src}")
                        set(full_path "${src}")
                else()
                        set(full_path "${tgt_source_dir}/${src}")
                endif()
                # skip files in the build path (in case bison/lex/etc)
                if(full_path MATCHES "^${CMAKE_BINARY_DIR}")
                        continue()
                endif()
                # Skip missing files
                if(NOT EXISTS "${full_path}")
                        continue()
                endif()

                list(APPEND acc_sources "${full_path}")

                get_cached_headers("${full_path}" cached_hdrs)
                if(cached_hdrs)
                        # message(STATUS "using cached headers ")
                        list(APPEND acc_headers ${cached_hdrs})
                        continue()
                endif()
                # message(STATUS "finding new headers")

                # Gather this target’s include directories:
                set(all_includes "")
                get_target_property(public_includes ${tgt} INTERFACE_INCLUDE_DIRECTORIES)
                get_target_property(private_includes ${tgt} INCLUDE_DIRECTORIES)

                if(public_includes)
                        list(APPEND all_includes ${public_includes})
                endif()
                if(private_includes)
                        list(APPEND all_includes ${private_includes})
                endif()
                if(EXISTS "${tgt_binary_dir}")
                        list(APPEND all_includes "${tgt_binary_dir}")
                endif()

                # Now pull in interface includes from any linked libraries
                get_target_property(linked_libs ${tgt} LINK_LIBRARIES)
                foreach(lib IN LISTS linked_libs)
                        if(TARGET ${lib})
                                get_target_property(lib_iface_inc ${lib}
                                                    INTERFACE_INCLUDE_DIRECTORIES)
                                if(lib_iface_inc)
                                        list(APPEND all_includes ${lib_iface_inc})
                                endif()
                        endif()
                endforeach()

                # Deduplicate and filter
                list(REMOVE_DUPLICATES all_includes)
                # message(STATUS "all : ${all_includes}")

                set(filtered_includes "")
                foreach(dir IN LISTS all_includes)
                        if(NOT dir MATCHES "-NOTFOUND" AND IS_DIRECTORY "${dir}")
                                list(APPEND filtered_includes "${dir}")
                        endif()
                endforeach()
                # message(STATUS "filtered : ${filtered_includes}")

                # Convert to -I flags
                set(include_flags "")
                foreach(dir IN LISTS filtered_includes)
                        if(NOT dir MATCHES "-NOTFOUND")
                                list(APPEND include_flags "-I${dir}")
                        endif()
                endforeach()

                # Grab both private and interface definitions
                collect_all_compile_defs_and_opts(${tgt} ALL_DEFS ALL_OPTS)
                # message(STATUS "All compile definitions: ${ALL_DEFS}")
		# message(STATUS "All compile options: ${ALL_OPTS}")

                # Merge into one list
                set(all_defs "")

                # Turn each into a -D flag, skipping gen-expr defs
                set(def_flags "")
                foreach(def IN LISTS ALL_DEFS)
                        list(APPEND def_flags "${def_prefix}${def}")
                endforeach()

                if(APPLE)
                        list(APPEND def_flags "${def_prefix}__APPLE__")
                endif()

                # Decide which compiler to use
                get_filename_component(file_ext "${full_path}" EXT)
                if(file_ext STREQUAL ".cpp"
                   OR file_ext STREQUAL ".cxx"
                   OR file_ext STREQUAL ".cc"
                   OR file_ext STREQUAL ".hpp"
                   OR file_ext STREQUAL ".hxx"
                   OR file_ext STREQUAL ".hh")
                        set(preproc_compiler "${CMAKE_CXX_COMPILER}")
                        set(preproc_flags "${CMAKE_CXX_FLAGS}")
                else()
                        set(preproc_compiler "${CMAKE_C_COMPILER}")
                        set(preproc_flags "${CMAKE_C_FLAGS}")
                endif()

                string(JOIN " " include_flags_str ${include_flags})
                string(JOIN " " def_flags_str ${def_flags})
                string(JOIN " " opt_flags_str ${ALL_OPTS})
                message(STATUS "cmd : ${preproc_compiler} ${pp_flags} ${include_trace_flag} ${include_flags} ${def_flags} ${sdk_flag} ${full_path} ${preproc_flags} ${opt_flags_str} ${std_flag}")

                execute_process(
                        COMMAND
                                ${preproc_compiler} ${pp_flags} ${include_trace_flag}
                                ${ALL_OPTS} ${include_flags} ${def_flags} ${sdk_flag}
                                ${full_path} ${preproc_flags} ${std_flag}
                        OUTPUT_VARIABLE preprocess_out
                        ERROR_VARIABLE preprocess_err
                        RESULT_VARIABLE preprocess_result
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)

                # message(STATUS "out : ${preprocess_out}")
		# message(STATUS "err : ${preprocess_err}")
		# message(STATUS "var : ${preprocess_result}")

                # Select where the trace landed
                if(MSVC)
                        set(include_trace "${preprocess_out}")
                else()
                        set(include_trace "${preprocess_err}")
                endif()

                # Debug output
                if(NOT preprocess_result EQUAL 0)
                        message(STATUS "Preprocessing failed for ${full_path}:\n${preprocess_err}")
                        continue() # Skip this source file if it can't be preprocessed
                endif()

                # Extract headers from preprocessor output
                string(REGEX MATCHALL "[. ]+/[^ \n\r\"]+\\.h" includes "${include_trace}")

                # message(STATUS "includes : ${includes}")

                # Only keep headers under our source dir, dedupe on the fly
                foreach(raw IN LISTS includes)
                        # Strip leading dots/spaces
                        string(REGEX REPLACE "^[. ]+" "" clean_path "${raw}")

                        # Only keep headers inside the project root
                        if(clean_path MATCHES "^${CMAKE_SOURCE_DIR}"
                           AND NOT clean_path MATCHES "^${CMAKE_BINARY_DIR}")
                                # Avoid duplicates and check existence
                                if(EXISTS "${clean_path}" AND NOT clean_path IN_LIST
                                                              seen_headers)
                                        list(APPEND acc_headers "${clean_path}")
                                        list(APPEND seen_headers "${clean_path}")
                                endif()
                        endif()
                endforeach()

                string(JOIN ";" joined "${acc_headers}")
                set_cached_headers("${full_path}" "${joined}")

                # Debug print of final header list
                # message(STATUS "Collected headers: ${acc_headers}")
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

        # Finally, merge sources + headers, remove duplicates, and return
        list(APPEND acc_sources ${acc_headers})
        list(REMOVE_DUPLICATES acc_sources)
        # message(STATUS "sending ${acc_sources}")
        set(${out_list}
            "${acc_sources}"
            PARENT_SCOPE)
endfunction()

# This provides a CMake function to check for SPDX-License-Identifier headers in the source
# files associated with a specified CMake target.
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

        # Preprocessor flags per compiler
        if(MSVC)
                set(include_trace_flag "/showIncludes")
                set(pp_flags "/E") # or /P for file
                set(def_prefix "/D")
        else()
                set(include_trace_flag "-H")
                set(pp_flags "-c")
                set(def_prefix "-D")
        endif()

        collect_sources(${target} all_sources)
        # message(STATUS "source : ${all_sources}")

        # Normalize source dir for filtering and relative path printing
        get_filename_component(abs_source_dir "${CMAKE_SOURCE_DIR}" ABSOLUTE)

        unset(missing_spdx)
        unset(spdx_map)
        unset(spdx_licenses)

        foreach(abs_source_file IN LISTS all_sources)
                # Skip non-existent files quietly
                if(NOT EXISTS "${abs_source_file}")
                        message(STATUS "missing ${abs_source_file}")
                        continue()
                endif()

                # Skip files outside the source directory (e.g. build/generated)
                string(FIND "${abs_source_file}" "${abs_source_dir}" pos)
                if(NOT pos EQUAL 0)
                        message(STATUS "skipping ${abs_source_file}")
                        continue()
                endif()

                file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}" "${abs_source_file}")
                set(cached_license "")
                get_cached_license("${abs_source_file}" cached_license)
                if(cached_license)
                        set(license "${cached_license}")
                else()

                        # Read content and extract license
                        file(READ "${abs_source_file}" file_content)
                        string(FIND "${file_content}" "SPDX-License-Identifier:" license_pos)

                        if(license_pos EQUAL -1)
                                list(APPEND missing_spdx "${rel_path}")
                                continue()
                        endif()
                        string(SUBSTRING "${file_content}" ${license_pos} 100 license_segment)
                        string(REGEX MATCH "SPDX-License-Identifier:[ \t]*([^ \t\r\n]+)"
                                     _match "${license_segment}")

                        if(CMAKE_MATCH_COUNT EQUAL 0)
                                set(license "Missing")
                        else()
                                set(license "${CMAKE_MATCH_1}")
                        endif()
                        set_cached_license("${abs_source_file}" "${license}")
                endif()
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
        endforeach()

        get_target_property(linked_libs ${target} LINK_LIBRARIES)

        set(external_libs "")
        foreach(lib IN LISTS linked_libs)
                if(NOT TARGET ${lib})
                        list(APPEND external_libs "${lib}")
                endif()
        endforeach()

        if(NOT spdx_licenses
           AND NOT missing_spdx
           AND NOT external_libs)
                # message(STATUS "skipping ${target}")
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

        # message(STATUS "========== SPDX Check END: ${target} ==========")
endfunction()

# Recursive function to collect all targets in a directory and its subdirectories
function(spdx_check_all_targets root_dir)
        # Recursive helper
        function(_spdx_check_in_dir dir)
                # Get all build system targets in this directory
                get_property(
                        local_targets
                        DIRECTORY ${dir}
                        PROPERTY BUILDSYSTEM_TARGETS)
                if(local_targets)
                        foreach(target IN LISTS local_targets)
                                spdx_check_target(${target})
                        endforeach()
                endif()

                # Recurse into subdirectories
                get_property(
                        subdirs
                        DIRECTORY ${dir}
                        PROPERTY SUBDIRECTORIES)
                foreach(subdir IN LISTS subdirs)
                        _spdx_check_in_dir(${subdir})
                endforeach()
        endfunction()

        _spdx_check_in_dir(${root_dir})
endfunction()
