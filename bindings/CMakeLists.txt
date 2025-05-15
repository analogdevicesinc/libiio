if (NOT CPP_BINDINGS AND CPP_EXAMPLES)
	message(STATUS "Turning off c++-bindings also disables feature: c++-examples")
	set(CPP_EXAMPLES OFF)
endif()

if (CPP_BINDINGS AND CPP_EXAMPLES)
        add_subdirectory(cpp)
endif()

if (CSHARP_BINDINGS)
	add_subdirectory(csharp)
endif()

if (PYTHON_BINDINGS)
	add_subdirectory(python)
endif()

toggle_iio_feature("${PYTHON_BINDINGS}" python-bindings)
toggle_iio_feature("${CSHARP_BINDINGS}" "c#-bindings")
toggle_iio_feature("${CPP_BINDINGS}" "c++-bindings")
toggle_iio_feature("${CPP_EXAMPLES}" "c++-examples")

set(IIOB_FEATURES_ON "${IIO_FEATURES_ON}" PARENT_SCOPE)
set(IIOB_FEATURES_OFF "${IIO_FEATURES_OFF}" PARENT_SCOPE)
