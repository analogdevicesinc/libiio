# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# Resolve BOARD into the variables no-OS needs (PLATFORM, TARGET, TARGET_NUM,
# BOARD_CONFIG_FILE, USE_VENDOR_TOOLCHAIN, ...) by reading the board preset
# no-OS itself ships in board_configs/<platform>/CMakePresets.json. The board
# list is never copied here: a board no-OS adds is buildable as soon as the
# checkout has it, on any platform, with -DBOARD=<name>.
#
# Runs before project(), after cmake/no_os_locate.cmake.

# Sets, in the caller's scope, every cacheVariable of preset `name` in the
# JSON document `json`, parents (inherits) first so the board overrides them.
function(_no_os_apply_preset json name)
  string(JSON _n LENGTH "${json}" configurePresets)
  math(EXPR _last "${_n} - 1")
  foreach(_i RANGE ${_last})
    string(JSON _name GET "${json}" configurePresets ${_i} name)
    if(NOT _name STREQUAL name)
      continue()
    endif()

    string(JSON _inherits ERROR_VARIABLE _err GET "${json}" configurePresets ${_i} inherits)
    if(NOT _err)
      string(JSON _type TYPE "${json}" configurePresets ${_i} inherits)
      if(_type STREQUAL "ARRAY")
        string(JSON _m LENGTH "${_inherits}")
        math(EXPR _mlast "${_m} - 1")
        foreach(_j RANGE ${_mlast})
          string(JSON _parent GET "${_inherits}" ${_j})
          _no_os_apply_preset("${json}" "${_parent}")
        endforeach()
      else()
        _no_os_apply_preset("${json}" "${_inherits}")
      endif()
    endif()

    string(JSON _vars ERROR_VARIABLE _err GET "${json}" configurePresets ${_i} cacheVariables)
    if(_err)
      break()
    endif()
    string(JSON _k LENGTH "${_vars}")
    if(_k EQUAL 0)
      break()
    endif()
    math(EXPR _klast "${_k} - 1")
    foreach(_j RANGE ${_klast})
      string(JSON _key MEMBER "${_vars}" ${_j})
      string(JSON _type TYPE "${_vars}" "${_key}")
      if(_type STREQUAL "OBJECT")
        string(JSON _val GET "${_vars}" "${_key}" value)
      else()
        string(JSON _val GET "${_vars}" "${_key}")
      endif()
      set(${_key} "${_val}" PARENT_SCOPE)
      set(_no_os_board_keys ${_no_os_board_keys} ${_key})
    endforeach()
    break()
  endforeach()
  set(_no_os_board_keys ${_no_os_board_keys} PARENT_SCOPE)
endfunction()

set(_no_os_board_keys "")
set(_boards "")
file(GLOB _preset_files "${NO_OS_PATH}/board_configs/*/CMakePresets.json")
foreach(_file ${_preset_files})
  file(READ "${_file}" _json)
  string(JSON _n LENGTH "${_json}" configurePresets)
  math(EXPR _last "${_n} - 1")
  foreach(_i RANGE ${_last})
    string(JSON _name GET "${_json}" configurePresets ${_i} name)
    string(JSON _hidden ERROR_VARIABLE _err GET "${_json}" configurePresets ${_i} hidden)
    if(_hidden STREQUAL "ON")
      continue()
    endif()
    list(APPEND _boards "${_name}")
    if(_name STREQUAL BOARD)
      _no_os_apply_preset("${_json}" "${BOARD}")
      set(_board_file "${_file}")
    endif()
  endforeach()
endforeach()

if(NOT _board_file)
  list(SORT _boards)
  string(REPLACE ";" " " _boards "${_boards}")
  message(FATAL_ERROR
    "BOARD=${BOARD} is not a board of ${NO_OS_PATH}/board_configs. "
    "Known boards: ${_boards}")
endif()

foreach(var PLATFORM BOARD_CONFIG_FILE)
  if(NOT ${var})
    message(FATAL_ERROR "the no-OS preset for ${BOARD} (${_board_file}) sets no ${var}.")
  endif()
endforeach()

# CMake keeps the compiler and the toolchain flags of the first configure, so
# a build dir cannot change board afterwards: say so instead of building the
# new board with the old one's flags.
if(DEFINED CACHE{IIOD_CONFIGURED_BOARD} AND NOT IIOD_CONFIGURED_BOARD STREQUAL BOARD)
  message(FATAL_ERROR
    "${CMAKE_BINARY_DIR} was configured for ${IIOD_CONFIGURED_BOARD}, not ${BOARD}. "
    "Reconfigure from scratch: cmake --preset <transport> --fresh -DBOARD=${BOARD}")
endif()
set(IIOD_CONFIGURED_BOARD "${BOARD}" CACHE INTERNAL "board this build dir was configured for")

# Cache them so the toolchain file sees them in try_compile too. A -D on the
# command line for one of these still wins: it is already in the cache.
list(REMOVE_DUPLICATES _no_os_board_keys)
foreach(_key ${_no_os_board_keys})
  if(DEFINED CACHE{${_key}})
    set(${_key} "$CACHE{${_key}}")
  else()
    set(${_key} "${${_key}}" CACHE STRING "from the no-OS preset of ${BOARD}")
  endif()
endforeach()
message(STATUS "board: ${BOARD} (PLATFORM=${PLATFORM}, TARGET=${TARGET}, ${BOARD_CONFIG_FILE})")

unset(_no_os_board_keys)
unset(_boards)
unset(_preset_files)
unset(_board_file)
unset(_json)
unset(_file)
unset(_name)
unset(_hidden)
unset(_err)
unset(_n)
unset(_i)
unset(_last)
