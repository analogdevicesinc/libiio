find_program(
	MATLAB_EXECUTABLE
	NAMES matlab
	DOC "Matlab main program"
)
mark_as_advanced(MATLAB_EXECUTABLE)

option(WITH_MATLAB_BINDINGS_API "Enable MATLAB bindings API" ON)

if (MATLAB_EXECUTABLE AND NOT SKIP_INSTALL_ALL)
	message(STATUS "Found MATLAB: Building bindings")
	install(
		DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
		DESTINATION "${CMAKE_INSTALL_PREFIX}/share/libiio"
		PATTERN "CMakeLists.txt" EXCLUDE
	)
	install(
		CODE "execute_process(
			COMMAND ${MATLAB_EXECUTABLE} -nodesktop
				-nodisplay -r \"cd('${CMAKE_INSTALL_PREFIX}/share/libiio/matlab');iio_installer_script;exit;\"
			OUTPUT_QUIET)")
	set(WITH_MATLAB_BINDINGS_API ON CACHE BOOL "" FORCE)
else()
	message(FATAL_ERROR "MATLAB search failed : Can not build bindings")
endif()
