if (CSHARP_BINDINGS)
	add_subdirectory(csharp)
endif()

if (PYTHON_BINDINGS)
	add_subdirectory(python)
endif()

list(APPEND IIOB_FEATURES_${PYTHON_BINDINGS} python-bindings)
list(APPEND IIOB_FEATURES_${CSHARP_BINDINGS} "c#-bindings")

set(IIOB_FEATURES_ON "${IIOB_FEATURES_ON}" PARENT_SCOPE)
set(IIOB_FEATURES_OFF "${IIOB_FEATURES_OFF}" PARENT_SCOPE)
