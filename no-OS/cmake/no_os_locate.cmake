# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# Locate an upstream no-OS checkout (github.com/analogdevicesinc/no-OS) with
# the CMake + Kconfig build, and set NO_OS_PATH to it. Runs before project():
# the toolchain file lives in that checkout.
#
# Order, first hit wins:
#   1. -DNO_OS_PATH=<dir>
#   2. the NO_OS_PATH, NO_OS_DIR or NOOS_DIR environment variable
#   3. a checkout next to libiio:        <libiio>/../no-OS, <libiio>/../*/no-OS
#   4. a checkout under $HOME:           ~/no-OS, ~/*/no-OS
#   5. -DNO_OS_FETCH=ON: shallow clone of NO_OS_GIT_TAG into the build dir
#
# A tree only counts when it has the new build (board_configs/, the Kconfig
# generator, the platform toolchains), so an old Make-only checkout lying
# around is skipped instead of failing half way through the configure. The
# platform is not known yet here: it comes from the board, which is looked up
# in the tree this file picks (cmake/no_os_board.cmake).

set(NO_OS_GIT_URL "https://github.com/analogdevicesinc/no-OS.git" CACHE STRING
    "no-OS repository cloned when NO_OS_FETCH is on")
set(NO_OS_GIT_TAG "main" CACHE STRING "no-OS branch or tag cloned when NO_OS_FETCH is on")
option(NO_OS_FETCH "Clone no-OS into the build dir when no local checkout is found" OFF)

function(_no_os_tree_ok dir out)
  if(EXISTS "${dir}/CMakeLists.txt" AND
     EXISTS "${dir}/board_configs" AND
     EXISTS "${dir}/tools/scripts/generate_config.py" AND
     EXISTS "${dir}/cmake/project_utils.cmake")
    set(${out} TRUE PARENT_SCOPE)
  else()
    set(${out} FALSE PARENT_SCOPE)
  endif()
endfunction()

if(DEFINED NO_OS_PATH)
  set(_no_os_how "-DNO_OS_PATH")
else()
  foreach(_env NO_OS_PATH NO_OS_DIR NOOS_DIR)
    if(DEFINED ENV{${_env}})
      set(NO_OS_PATH "$ENV{${_env}}")
      set(_no_os_how "\$${_env}")
      break()
    endif()
  endforeach()
endif()

if(DEFINED NO_OS_PATH)
  get_filename_component(NO_OS_PATH "${NO_OS_PATH}" REALPATH)
  _no_os_tree_ok("${NO_OS_PATH}" _ok)
  if(NOT _ok)
    message(FATAL_ERROR
      "${_no_os_how} points at ${NO_OS_PATH}, which is not a no-OS checkout "
      "with the CMake build (needs board_configs/, cmake/project_utils.cmake "
      "and tools/scripts/generate_config.py).")
  endif()
else()
  set(_no_os_how "search")
  get_filename_component(_libiio "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
  file(GLOB _globbed LIST_DIRECTORIES true
       "${_libiio}/../*/no-OS" "$ENV{HOME}/*/no-OS")
  set(_found "")
  foreach(_dir "${_libiio}/../no-OS" "$ENV{HOME}/no-OS" ${_globbed})
    if(NOT IS_DIRECTORY "${_dir}")
      continue()
    endif()
    get_filename_component(_dir "${_dir}" REALPATH)
    _no_os_tree_ok("${_dir}" _ok)
    if(_ok)
      list(APPEND _found "${_dir}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _found)

  if(_found)
    list(GET _found 0 NO_OS_PATH)
    list(LENGTH _found _n)
    if(_n GREATER 1)
      string(REPLACE ";" "\n     " _list "${_found}")
      message(WARNING
        "several no-OS checkouts found, using the first:\n     ${_list}\n"
        "Pass -DNO_OS_PATH=<dir> or export NO_OS_PATH to pick another.")
    endif()
  elseif(NO_OS_FETCH)
    set(NO_OS_PATH "${CMAKE_BINARY_DIR}/_deps/no-os")
    set(_no_os_how "clone of ${NO_OS_GIT_TAG}")
    if(NOT EXISTS "${NO_OS_PATH}/CMakeLists.txt")
      find_package(Git REQUIRED)
      message(STATUS "Cloning ${NO_OS_GIT_URL} (${NO_OS_GIT_TAG}) into ${NO_OS_PATH}")
      execute_process(
        COMMAND ${GIT_EXECUTABLE} clone --depth 1 --branch ${NO_OS_GIT_TAG}
                ${NO_OS_GIT_URL} ${NO_OS_PATH}
        RESULT_VARIABLE _rc)
      if(_rc)
        message(FATAL_ERROR "cloning no-OS failed (${_rc}).")
      endif()
    endif()
    _no_os_tree_ok("${NO_OS_PATH}" _ok)
    if(NOT _ok)
      message(FATAL_ERROR
        "the cloned no-OS (${NO_OS_GIT_TAG}) has no CMake build.")
    endif()
  else()
    message(FATAL_ERROR
      "no no-OS checkout with the CMake build found. "
      "Looked for ../no-OS and ../*/no-OS next to libiio, and ~/no-OS, "
      "~/*/no-OS. Pass -DNO_OS_PATH=<dir>, export NO_OS_PATH, or configure "
      "with -DNO_OS_FETCH=ON to clone it into the build dir.")
  endif()
endif()

set(NO_OS_PATH "${NO_OS_PATH}" CACHE PATH "upstream no-OS checkout" FORCE)
message(STATUS "no-OS: ${NO_OS_PATH} (${_no_os_how})")

unset(_no_os_how)
unset(_libiio)
unset(_globbed)
unset(_found)
unset(_dir)
unset(_env)
unset(_list)
unset(_ok)
unset(_n)
unset(_rc)
