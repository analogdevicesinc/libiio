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

list(APPEND OPTIONS_LISTS
	"CSHARP_BINDINGS"
	"CPP_BINDINGS"
	"CPP_EXAMPLES"
)
list(APPEND FEATURES_LISTS
	"c#-bindings"
	"c++-bindings"
	"c++-examples"
)

set(IIOB_OPTIONS "${OPTIONS_LISTS}" PARENT_SCOPE)
set(IIOB_FEATURES "${FEATURES_LISTS}" PARENT_SCOPE)
