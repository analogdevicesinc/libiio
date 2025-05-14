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

list(APPEND OPTIONS_LISTS
	"PYTHON_BINDINGS"
	"CSHARP_BINDINGS"
	"CPP_BINDINGS"
	"CPP_EXAMPLES"
)
list(APPEND FEATURES_LISTS
	"python-bindings"
	"c#-bindings"
	"c++-bindings"
	"c++-examples"
)

set(IIOB_OPTIONS "${OPTIONS_LISTS}" PARENT_SCOPE)
set(IIOB_FEATURES "${FEATURES_LISTS}" PARENT_SCOPE)
