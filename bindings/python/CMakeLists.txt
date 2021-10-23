cmake_minimum_required(VERSION 2.8.7)
project(libiio-py NONE)

if(${CMAKE_VERSION} VERSION_LESS "3.12.0") 
	message(STATUS "bin=${PYTHON_EXECUTABLE} lib=${PYTHON_LIBRARY} inc=${PYTHON_INCLUDE_DIR}")
	include(FindPythonInterp)

	# Set variables as they would be set by module FindPython,
	# which is available from CMake 3.12.
	set(Python_Interpreter_FOUND ${PYTHONINTERP_FOUND})
	set(Python_EXECUTABLE ${PYTHON_EXECUTABLE})
else()
	if(PYTHON_EXECUTABLE)
		set(Python_EXECUTABLE ${PYTHON_EXECUTABLE})
	endif()
	if (PYTHON_LIBRARY)
		set(Python_LIBRARY ${PYTHON_LIBRARY})
	endif()
	if (PYTHON_INCLUDE_DIR)
		set(Python_INCLUDE_DIR ${PYTHON_INCLUDE_DIR})
	endif()
	message(STATUS "bin=${Python_EXECUTABLE} lib=${Python_LIBRARY} inc=${Python_INCLUDE_DIR}")
	find_package (Python COMPONENTS Interpreter)
	message(STATUS "Python_EXECUTABLE ${Python_EXECUTABLE}")
endif()

if (Python_Interpreter_FOUND)
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

if(WITH_DOC)
	if(${CMAKE_VERSION} VERSION_LESS "3.2.0")
		# cmake -E env was added in 3.2
		message(FATAL_ERROR "Sorry, you can't build python doc with ancient cmake, please update")
	endif()
	find_program(SPHINX_EXECUTABLE
		NAMES sphinx-build
		DOC "Sphinx Documentation Builder (sphinx-doc.org)"
	)
	if (NOT SPHINX_EXECUTABLE)
		message(FATAL_ERROR "Can not build python doc without sphinx-build")
	endif()
	message (STATUS "Building with Python Doc (Sphinx)")
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/doc/conf.py.in
		${CMAKE_CURRENT_SOURCE_DIR}/doc/conf.py)
	add_custom_command(TARGET libiio-py POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=$ENV{LD_LIBRARY_PATH}:${CMAKE_BINARY_DIR}"
		${SPHINX_EXECUTABLE}
			-b html
			-n
			-c ${CMAKE_CURRENT_SOURCE_DIR}/doc
			-d ${CMAKE_CURRENT_BINARY_DIR}/doctrees
			-w ${CMAKE_BINARY_DIR}/Spx_output_python
			${CMAKE_CURRENT_SOURCE_DIR}/doc
			${CMAKE_HTML_DEST_DIR}/python
		COMMENT "Generating Python binding documentation with Sphinx" VERBATIM
	)
endif()
