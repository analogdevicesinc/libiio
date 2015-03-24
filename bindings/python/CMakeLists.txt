cmake_minimum_required(VERSION 2.8.7)
project(libiio-py)

include(FindPythonInterp)

if (PYTHONINTERP_FOUND)
	option(PYTHON_BINDINGS "Install Python bindings" ON)

	if (PYTHON_BINDINGS)
		set(SETUP_PY_IN ${CMAKE_CURRENT_SOURCE_DIR}/setup.py.cmakein)
		set(SETUP_PY ${CMAKE_CURRENT_BINARY_DIR}/setup.py)

		configure_file(${SETUP_PY_IN} ${SETUP_PY})

		install(CODE "execute_process(COMMAND ${PYTHON_EXECUTABLE} ${SETUP_PY} install --root=\$ENV{DESTDIR}/ --prefix=${CMAKE_INSTALL_PREFIX})")
	endif()
endif()
