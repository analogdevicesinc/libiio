cmake_minimum_required(VERSION 2.8.7)
project(libiio-sharp NONE)

find_program(MCS_EXECUTABLE
	NAMES mcs csc
	HINTS "C:/Windows/Microsoft.NET/Framework/v4.0.30319"
	PATHS ENV MCS_EXECUTABLE_PATH
	DOC "C# compiler")
mark_as_advanced(MCS_EXECUTABLE)

if (MCS_EXECUTABLE)
	option(CSHARP_BINDINGS "Install C# bindings" ON)

	if (CSHARP_BINDINGS)
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
			install(FILES ${LIBIIO_CS_DLL} ${LIBIIO_CS_DLL}.mdb DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cli/libiio-sharp-${VERSION})
		endif()
	endif()
endif()
