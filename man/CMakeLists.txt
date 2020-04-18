if (WITH_MAN)
	find_program(BASH_EXECUTABLE bash)
	find_program(DATE_EXECUTABLE date)
	execute_process(
		COMMAND ${DATE_EXECUTABLE} "+%d %B %Y"
		OUTPUT_VARIABLE CMAKE_DATE OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(
		COMMAND ${BASH_EXECUTABLE} "-c" "${CMAKE_CURRENT_SOURCE_DIR}/make_man.sh > ${CMAKE_BINARY_DIR}/libiio.3.in"
		)
	configure_file(
		${CMAKE_BINARY_DIR}/libiio.3.in
		${CMAKE_BINARY_DIR}/man/libiio.3 @ONLY)

	if (WITH_DOC)
		find_program(MAN2HTML man2html)
		if (NOT MAN2HTML)
			message(FATAL_ERROR "Can not build html DOC from man without man2html")
		endif()
		message(STATUS "Building html doc pages with man2html")
		file(MAKE_DIRECTORY ${CMAKE_HTML_DEST_DIR}/man3)
		execute_process(
			COMMAND ${MAN2HTML} -r
			INPUT_FILE ${CMAKE_BINARY_DIR}/man/libiio.3
			OUTPUT_FILE ${CMAKE_HTML_DEST_DIR}/man3/libiio.3.html
		)
	endif()
	if (WITH_TESTS)
		list(APPEND MAN1 iio_attr.1 iio_info.1 iio_readdev.1 iio_reg.1 iio_writedev.1)
		foreach(_page ${MAN1})
			configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/${_page}.in
				${CMAKE_BINARY_DIR}/man/${_page} @ONLY)
		endforeach()
		if (WITH_DOC)
			file(MAKE_DIRECTORY ${CMAKE_HTML_DEST_DIR}/man1)
			foreach(_page ${MAN1})
				execute_process(
					COMMAND ${MAN2HTML} -r
					INPUT_FILE ${CMAKE_BINARY_DIR}/man/${_page}
					OUTPUT_FILE ${CMAKE_HTML_DEST_DIR}/man1/${_page}.html
				)
			endforeach()
		endif()
	endif()
	# install man files into the BINARY directories,
	# section 3 = library functions
	install(DIRECTORY ${CMAKE_BINARY_DIR}/man/
		DESTINATION ${CMAKE_INSTALL_MANDIR}/man3
		COMPONENT doc FILES_MATCHING PATTERN "*.3*")
	# section 1 = user commands
	install(DIRECTORY ${CMAKE_BINARY_DIR}/man/
		DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
		COMPONENT doc FILES_MATCHING PATTERN "*.1*")

endif()
