if (CPP_BINDINGS)
        add_subdirectory(cpp)
endif()

if (CSHARP_BINDINGS)
	add_subdirectory(csharp)
endif()

toggle_iio_feature("${CSHARP_BINDINGS}" "c#-bindings")
toggle_iio_feature("${CPP_BINDINGS}" "c++-bindings")

set(IIOB_FEATURES_ON "${IIO_FEATURES_ON}" PARENT_SCOPE)
set(IIOB_FEATURES_OFF "${IIO_FEATURES_OFF}" PARENT_SCOPE)
