# check to make sure man pages are there, otherwise build them

macro( CheckManPages FILE_LIST )
	if(WITH_MAN AND NOT CMAKE_CROSSCOMPILING)
		foreach(UTIL IN ITEMS ${FILE_LIST})
			if(NOT EXISTS "${CMAKE_SOURCE_DIR}/man/${UTIL}.1.in")
				if(NOT DEFINED HELP2MAN OR HELP2MAN STREQUAL "")
					find_program(HELP2MAN "help2man")
				endif()
				if(HELP2MAN)
					if(NOT TARGET ${UTIL}.1)
						add_custom_target("${UTIL}.1" ALL
							COMMAND ${HELP2MAN}
								-n 'Part of the libiio utils'
								--no-info --no-discard-stderr
								-o ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_MANDIR}/${UTIL}.1
								${CMAKE_CURRENT_BINARY_DIR}/${UTIL}
								COMMENT "Generating man page for ${UTIL}"
							DEPENDS "${UTIL}"
							WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
						)
					endif()
				else()
					message(STATUS "can not generate ${UTIL} man page, missing help2man")
				endif()
			endif()
		endforeach()
	endif()
endmacro()


