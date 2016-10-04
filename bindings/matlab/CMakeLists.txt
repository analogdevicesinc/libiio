find_program(
	MATLAB_EXECUTABLE
	NAMES matlab
	DOC "Matlab main program"
)
mark_as_advanced(MATLAB_EXECUTABLE)

if (MATLAB_EXECUTABLE)
	option(MATLAB_BINDINGS "Install MATLAB bindings" ON)

	if (MATLAB_BINDINGS)
		install(
			DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
			DESTINATION "${CMAKE_INSTALL_PREFIX}/share/libiio"
			PATTERN "CMakeLists.txt" EXCLUDE
		)
	endif()
endif()
