cmake_minimum_required(VERSION 3.10)
project(libiio-sharp NONE)

if (WIN32)
	set(MCS_EXECUTABLE_NAME csc)
else()
	set(MCS_EXECUTABLE_NAME mcs)
endif()

find_program(MCS_EXECUTABLE
	NAMES ${MCS_EXECUTABLE_NAME}
	HINTS "C:/Windows/Microsoft.NET/Framework/v4.0.30319"
	PATHS ENV MCS_EXECUTABLE_PATH
	DOC "C# compiler")
mark_as_advanced(MCS_EXECUTABLE)

if (MCS_EXECUTABLE)
	message(STATUS "Found C#: Building bindings")
	set(LIBIIO_CS_PC_IN "${CMAKE_CURRENT_SOURCE_DIR}/libiio-sharp.pc.cmakein")
	set(LIBIIO_CS_PC "${CMAKE_CURRENT_BINARY_DIR}/libiio-sharp-${VERSION}.pc")
	configure_file(${LIBIIO_CS_PC_IN} ${LIBIIO_CS_PC} @ONLY)
	if(NOT SKIP_INSTALL_ALL)
		install(FILES ${LIBIIO_CS_PC} DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig")
	endif()

	set(LIBIIO_CS_DLL_CONFIG_IN "${CMAKE_CURRENT_SOURCE_DIR}/libiio-sharp.dll.config.cmakein")
	set(LIBIIO_CS_DLL_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/libiio-sharp.dll.config")
	configure_file(${LIBIIO_CS_DLL_CONFIG_IN} ${LIBIIO_CS_DLL_CONFIG} @ONLY)
	if(NOT SKIP_INSTALL_ALL)
		install(FILES ${LIBIIO_CS_DLL_CONFIG} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cli/libiio-sharp-${VERSION})
	endif()

	set(LIBIIO_CS_VERSION ${VERSION}.0.0)
	set(LIBIIO_CS_INFO_IN ${CMAKE_CURRENT_SOURCE_DIR}/AssemblyInfo.cs.in)
	set(LIBIIO_CS_INFO ${CMAKE_CURRENT_BINARY_DIR}/AssemblyInfo.cs)
	configure_file(${LIBIIO_CS_INFO_IN} ${LIBIIO_CS_INFO} @ONLY)

	set(LIBIIO_CS_DLL "${CMAKE_CURRENT_BINARY_DIR}/libiio-sharp.dll")
	set(LIBIIO_CS_SOURCES
		${CMAKE_CURRENT_SOURCE_DIR}/Attr.cs
		${CMAKE_CURRENT_SOURCE_DIR}/Channel.cs
		${CMAKE_CURRENT_SOURCE_DIR}/Context.cs
		${CMAKE_CURRENT_SOURCE_DIR}/Device.cs
		${CMAKE_CURRENT_SOURCE_DIR}/IOBuffer.cs
		${CMAKE_CURRENT_SOURCE_DIR}/Trigger.cs
		${CMAKE_CURRENT_SOURCE_DIR}/IioLib.cs
		${CMAKE_CURRENT_SOURCE_DIR}/ScanContext.cs
		${LIBIIO_CS_INFO}
		)

	foreach(SRC ${LIBIIO_CS_SOURCES})
		file(TO_NATIVE_PATH ${SRC} TMP)
		set(LIBIIO_CS_SOURCES_REALPATH ${LIBIIO_CS_SOURCES_REALPATH} ${TMP})
	endforeach(SRC)

	file(TO_NATIVE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/key.snk SIGN_KEY)
	file(TO_NATIVE_PATH ${LIBIIO_CS_DLL} LIBIIO_CS_DLL_OUT)

	add_custom_command(OUTPUT ${LIBIIO_CS_DLL}
		COMMAND ${MCS_EXECUTABLE} /target:library /out:${LIBIIO_CS_DLL_OUT} /debug /keyfile:${SIGN_KEY} ${LIBIIO_CS_SOURCES_REALPATH}
		DEPENDS ${LIBIIO_CS_SOURCES}
		)

	add_custom_target(libiio-sharp ALL DEPENDS ${LIBIIO_CS_DLL})

	if(NOT SKIP_INSTALL_ALL)
		set(DEBUG_SYMBOLS_FILE "")
		if(MCS_EXECUTABLE_NAME STREQUAL "mcs")
			set(DEBUG_SYMBOLS_FILE "${LIBIIO_CS_DLL}.mdb")
		elseif(MCS_EXECUTABLE_NAME STREQUAL "csc")
			set(DEBUG_SYMBOLS_FILE "${CMAKE_CURRENT_BINARY_DIR}/libiio-sharp.pdb")
		endif()
		install(FILES ${LIBIIO_CS_DLL} ${DEBUG_SYMBOLS_FILE} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cli/libiio-sharp-${VERSION})
	endif()
else()
	message(FATAL_ERROR "C# compiler search failed : Can not build C# bindings")
endif()
