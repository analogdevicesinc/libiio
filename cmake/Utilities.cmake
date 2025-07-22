function(check_pthread_set_name HAS_PTHREAD)
        include(CheckSymbolExists)
        set(CMAKE_REQUIRED_LIBRARIES ${PTHREAD_LIBRARIES})
        set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
        set(TMP_FLAGS "${CMAKE_C_FLAGS}")
        set(CMAKE_C_FLAGS "")
        check_symbol_exists(pthread_setname_np "pthread.h" ${HAS_PTHREAD})
        set(CMAKE_C_FLAGS "${TMP_FLAGS}")
        set(CMAKE_REQUIRED_LIBRARIES)
        set(CMAKE_REQUIRED_DEFINITIONS)
endfunction()

# <variable>, <help_text>, and <value> are consistent with the arguments used in CMake's native option() function
# <group> can be: "POSIX" or "MCU"
macro(iio_option variable help_text value group)
	option(${variable} ${help_text} ${value})

	if(${WITH_LIBTINYIIOD})
		if("${group}" STREQUAL "POSIX")
			set_property(CACHE ${variable} PROPERTY VALUE OFF)
		elseif("${group}" STREQUAL "MCU")
			set_property(CACHE ${variable} PROPERTY VALUE ON)
		endif()
	endif()

endmacro()
