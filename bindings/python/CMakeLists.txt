cmake_minimum_required(VERSION 2.8.7)
project(libiio-py NONE)

find_package (Python COMPONENTS Interpreter)

if (Python_Interpreter_FOUND)
	option(PYTHON_BINDINGS "Install Python bindings" ON)

	if (PYTHON_BINDINGS)

		set(SETUP_PY_IN ${CMAKE_CURRENT_SOURCE_DIR}/setup.py.cmakein)
		set(SETUP_PY ${CMAKE_CURRENT_BINARY_DIR}/setup.py)

		configure_file(${SETUP_PY_IN} ${SETUP_PY})

		configure_file(${CMAKE_CURRENT_SOURCE_DIR}/iio.py  ${CMAKE_CURRENT_BINARY_DIR}/iio.py COPYONLY)

		add_custom_target(libiio-py ALL DEPENDS ${SETUP_PY} COMMAND ${Python_EXECUTABLE} ${SETUP_PY} --quiet build)

		if(NOT SKIP_INSTALL_ALL)
			install(CODE "execute_process(WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} COMMAND ${Python_EXECUTABLE} ${SETUP_PY} install --root=\$ENV{DESTDIR}/ --prefix=${CMAKE_INSTALL_PREFIX})")
		endif()
	endif()
endif()
