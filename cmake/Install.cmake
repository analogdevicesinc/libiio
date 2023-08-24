
set(CMAKE_INSTALL_DOCDIR "" CACHE PATH "documentation root (DATAROOTDIR/doc/${PROJECT_NAME}${LIBIIO_VERSION_MAJOR}-doc)")
if(ENABLE_SHARED AND ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")
	set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()
set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/${PROJECT_NAME}${LIBIIO_VERSION_MAJOR}-doc")

set(INSTALL_PKGCONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
	CACHE PATH "Installation directory for pkgconfig (.pc) files")
mark_as_advanced(INSTALL_PKGCONFIG_DIR)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	option(OSX_PACKAGE "Create a OSX package" ON)

	set(OSX_INSTALL_FRAMEWORKSDIR "/Library/Frameworks" CACHE STRING "Installation directory for frameworks")
	get_filename_component(OSX_INSTALL_FRAMEWORKSDIR "${OSX_INSTALL_FRAMEWORKSDIR}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")

	set(CMAKE_MACOSX_RPATH ON)
	set(SKIP_INSTALL_ALL ${OSX_PACKAGE})
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin" AND OSX_FRAMEWORK)
	set(IIO_TESTS_INSTALL_DIR ${OSX_INSTALL_FRAMEWORKSDIR}/iio.framework/Tools)

	add_custom_command(TARGET iio-compat POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E remove Current
		COMMAND ${CMAKE_COMMAND} -E create_symlink ${PROJECT_VERSION} Current
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/iio.framework/Versions
		COMMENT "Fixup Current symbolic link" VERBATIM)
else()
	set(IIO_TESTS_INSTALL_DIR ${CMAKE_INSTALL_BINDIR})
endif()

configure_file(artifact_manifest.txt.cmakein ${CMAKE_CURRENT_BINARY_DIR}/artifact_manifest.txt @ONLY)
configure_file(libiio.iss.cmakein ${CMAKE_CURRENT_BINARY_DIR}/libiio.iss @ONLY)

set(LIBIIO_PC ${CMAKE_CURRENT_BINARY_DIR}/libiio.pc)
configure_file(libiio.pc.cmakein ${LIBIIO_PC} @ONLY)

if(NOT SKIP_INSTALL_ALL)
	install(FILES ${LIBIIO_PC} DESTINATION "${INSTALL_PKGCONFIG_DIR}")

	install(TARGETS iio ${IIO_COMPAT_LIB}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
		PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iio
		FRAMEWORK DESTINATION ${OSX_INSTALL_FRAMEWORKSDIR})

	if (WITH_UTILS)
		#install(TARGETS ${IIO_TESTS_TARGETS}
		#	RUNTIME DESTINATION ${IIO_TESTS_INSTALL_DIR})

		# Workaround for CMake < 3.13, which do not support installing
		# targets built outside the current directory.
		set(IIO_TEST_PROGRAMS)
		foreach(_tool ${IIO_TESTS_TARGETS})
			list(APPEND IIO_TEST_PROGRAMS $<TARGET_FILE:${_tool}>)
		endforeach()
		install(PROGRAMS ${IIO_TEST_PROGRAMS} DESTINATION ${IIO_TESTS_INSTALL_DIR})
	endif()
endif()

option(WITH_DOC "Generate documentation with Doxygen" OFF)
if(WITH_DOC)
	find_package(Doxygen REQUIRED)
	# It is not an error when 'dot' is not found,
	# just switching off the Doxygen's HAVE_DOT option
	find_package_handle_standard_args(Dot REQUIRED_VARS DOXYGEN_DOT_EXECUTABLE)

	include(cmake/CheckCaseSensitiveFileSystem.cmake)
	if (HAVE_CASE_SENSITIVE_FILESYSTEM)
		set(CMAKE_CASE_SENSITIVE_FILESYSTEM "YES")
	else()
		set(CMAKE_CASE_SENSITIVE_FILESYSTEM "NO")
	endif()

	set(CMAKE_API_DEST_DIR "${PROJECT_NAME}")

	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
		${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/bindings/csharp/Doxyfile.in
		${CMAKE_CURRENT_BINARY_DIR}/Doxyfile_csharp @ONLY)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/CI/azure/generateDocumentationAndDeploy.sh.in
		${CMAKE_CURRENT_BINARY_DIR}/generateDocumentationAndDeploy.sh @ONLY)
	file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/doc
		DESTINATION ${CMAKE_HTML_DEST_DIR}/${CMAKE_API_DEST_DIR}
		FILES_MATCHING PATTERN "*.svg")
	file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/doc/html/ DESTINATION ${CMAKE_HTML_DEST_DIR})
	set(IIO_TESTS_MAN_PAGES_HTML "")
	foreach(_page ${IIO_TESTS_TARGETS})
		set(IIO_TESTS_MAN_PAGES_HTML "${IIO_TESTS_MAN_PAGES_HTML}<li><a href=\"./man1/${_page}.1.html\">${_page}</a></li>")
	endforeach()
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/doc/index.html.in
		${CMAKE_HTML_DEST_DIR}/index.html @ONLY)

	add_custom_command(TARGET iio POST_BUILD
		COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Generating API documentation with Doxygen" VERBATIM)
	add_custom_command(TARGET iio POST_BUILD
		COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile_csharp
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Generating C# documentation with Doxygen" VERBATIM)

	if(NOT SKIP_INSTALL_ALL)
		install(DIRECTORY ${CMAKE_HTML_DEST_DIR}
			DESTINATION ${CMAKE_INSTALL_DOCDIR})
	endif()
endif()

# Create an installer if compiling for OSX
if(OSX_PACKAGE)
	set(LIBIIO_PKG ${CMAKE_CURRENT_BINARY_DIR}/libiio-${LIBIIO_VERSION}.pkg)
	set(LIBIIO_TEMP_PKG ${CMAKE_CURRENT_BINARY_DIR}/libiio-${VERSION}-temp.pkg)
	set(LIBIIO_DISTRIBUTION_XML ${CMAKE_CURRENT_BINARY_DIR}/Distribution.xml)
	set(LIBIIO_FRAMEWORK_DIR ${CMAKE_CURRENT_BINARY_DIR}/iio.framework)
	configure_file(Distribution.xml.cmakein ${LIBIIO_DISTRIBUTION_XML} @ONLY)

	find_program(PKGBUILD_EXECUTABLE
		NAMES pkgbuild
		DOC "OSX Package builder (pkgbuild)")
	mark_as_advanced(PKGBUILD_EXECUTABLE)

	find_program(PRODUCTBUILD_EXECUTABLE
		NAMES productbuild
		DOC "OSX Package builder (productbuild)")
	mark_as_advanced(PRODUCTBUILD_EXECUTABLE)

	set(COPY_TOOLS_COMMAND)
	foreach(_tool ${IIO_TESTS_TARGETS})
		list(APPEND COPY_TOOLS_COMMAND
			COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${_tool}> ${LIBIIO_FRAMEWORK_DIR}/Tools)
	endforeach()

	add_custom_command(OUTPUT ${LIBIIO_PKG}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBIIO_FRAMEWORK_DIR}/Tools
		${COPY_TOOLS_COMMAND}
		COMMAND ${PKGBUILD_EXECUTABLE}
			--component ${LIBIIO_FRAMEWORK_DIR}
			--identifier com.adi.iio --version ${VERSION}
			--install-location ${OSX_INSTALL_FRAMEWORKSDIR} ${LIBIIO_TEMP_PKG}
		COMMAND ${PRODUCTBUILD_EXECUTABLE}
			--distribution ${LIBIIO_DISTRIBUTION_XML} ${LIBIIO_PKG}
		COMMAND ${CMAKE_COMMAND} -E remove ${LIBIIO_TEMP_PKG}
		DEPENDS iio ${IIO_TESTS_TARGETS} ${LIBIIO_DISTRIBUTION_XML}
	)

	if (PKGBUILD_EXECUTABLE AND PRODUCTBUILD_EXECUTABLE)
		add_custom_target(libiio-pkg ALL DEPENDS ${LIBIIO_PKG})

		install(CODE "execute_process(COMMAND /usr/sbin/installer -pkg ${LIBIIO_PKG} -target /)")
	else()
		message(WARNING "Missing pkgbuild or productbuild: OSX installer won't be created.")
	endif()
endif()

if (NOT OSX_PACKAGE)
	# Support creating some basic binpkgs via `make package`.
	# Disabled if OSX_PACKAGE is enabled, as tarballs would end up empty otherwise.
	option(ENABLE_PACKAGING "Create .deb/.rpm or .tar.gz packages via 'make package'" OFF)

	if(ENABLE_PACKAGING)
		if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
			include(cmake/DarwinPackaging.cmake)
		endif()
		if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
			include(cmake/LinuxPackaging.cmake)
		endif()
	endif()
endif()

if (WITH_USB_BACKEND AND CMAKE_SYSTEM_NAME MATCHES "^Linux")
	option(INSTALL_UDEV_RULE "Install a udev rule for detection of USB devices" ON)

	if (INSTALL_UDEV_RULE)
		set(UDEV_RULES_INSTALL_DIR /lib/udev/rules.d CACHE PATH "default install path for udev rules")

		configure_file(libiio.rules.cmakein ${CMAKE_CURRENT_BINARY_DIR}/90-libiio.rules @ONLY)
		install(FILES ${CMAKE_CURRENT_BINARY_DIR}/90-libiio.rules DESTINATION ${UDEV_RULES_INSTALL_DIR})
	endif()
endif()

if (WITH_NETWORK_BACKEND_DYNAMIC)
	install(TARGETS iio-ip LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/libiio)
endif()
if (WITH_SERIAL_BACKEND_DYNAMIC)
	install(TARGETS iio-serial LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/libiio)
endif()
if (WITH_USB_BACKEND_DYNAMIC)
	install(TARGETS iio-usb LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/libiio)
endif()
