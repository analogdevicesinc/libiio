cmake_minimum_required(VERSION 2.8.8)
project(iiod C)

option(ENABLE_IPV6 "Define if you want to enable IPv6 support" ON)

include(CheckSymbolExists)
check_symbol_exists(in6addr_any "netinet/in.h" HAVE_IPV6)

if (ENABLE_IPV6)
	if (NOT HAVE_IPV6)
		message(WARNING "IPv6 is not available in your system.")
	else()
		add_definitions(-DHAVE_IPV6=1)
	endif()
endif()

include(FindBISON)
include(FindFLEX)

flex_target(lexer
	${CMAKE_CURRENT_SOURCE_DIR}/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.c)
bison_target(parser
	${CMAKE_CURRENT_SOURCE_DIR}/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.c)
add_flex_bison_dependency(lexer parser)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(IIOD_CFILES iiod.c ops.c ${BISON_parser_OUTPUTS} ${FLEX_lexer_OUTPUTS})

add_executable(iiod ${IIOD_CFILES})
target_link_libraries(iiod iio ${PTHREAD_LIBRARIES} ${AVAHI_LIBRARIES})

if(NOT SKIP_INSTALL_ALL)
	install(TARGETS iiod RUNTIME DESTINATION sbin)
endif()
