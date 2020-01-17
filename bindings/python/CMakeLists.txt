cmake_minimum_required(VERSION 2.8.7)
project(libiio-py NONE)

if(${CMAKE_VERSION} VERSION_LESS "3.12.0") 
	include(FindPythonInterp)

	# Set variables as they would be set by module FindPython,
	# which is available from CMake 3.12.
	set(Python_Interpreter_FOUND ${PYTHONINTERP_FOUND})
	set(Python_EXECUTABLE ${PYTHON_EXECUTABLE})
else()
	find_package (Python COMPONENTS Interpreter)
endif()

if (Python_Interpreter_FOUND)
	message(STATUS "Found Python: Building bindings")
	set(SETUP_PY_IN ${CMAKE_CURRENT_SOURCE_DIR}/setup.py.cmakein)
	set(SETUP_PY ${CMAKE_CURRENT_BINARY_DIR}/setup.py)

	configure_file(${SETUP_PY_IN} ${SETUP_PY})

	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/iio.py  ${CMAKE_CURRENT_BINARY_DIR}/iio.py COPYONLY)

	add_custom_target(libiio-py ALL DEPENDS ${SETUP_PY} COMMAND ${Python_EXECUTABLE} ${SETUP_PY} --quiet build)

	if(NOT SKIP_INSTALL_ALL)
		install(CODE "execute_process(WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} COMMAND ${Python_EXECUTABLE} ${SETUP_PY} install --root=\$ENV{DESTDIR}/ --prefix=${CMAKE_INSTALL_PREFIX})")
	endif()
else()
	message(FATAL_ERROR "Python search failed : Can not build Python bindings")
endif()
