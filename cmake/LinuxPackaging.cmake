# support creating some basic binpkgs via `make package`
set(CPACK_SET_DESTDIR ON)
set(CPACK_GENERATOR TGZ;DEB)

FIND_PROGRAM(RPMBUILD_CMD rpmbuild)
if (RPMBUILD_CMD)
	set(CPACK_PACKAGE_RELOCATABLE OFF)
	set(CPACK_GENERATOR ${CPACK_GENERATOR};RPM)
endif()

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)
set(CPACK_PACKAGE_VERSION_MAJOR ${LIBIIO_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${LIBIIO_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH g${LIBIIO_VERSION_GIT})
set(CPACK_BUNDLE_NAME libiio)
set(CPACK_PACKAGE_VERSION ${LIBIIO_VERSION})
# debian specific package settings
set(CPACK_PACKAGE_CONTACT "Engineerzone <https://ez.analog.com/community/linux-device-drivers>")

option(DEB_DETECT_DEPENDENCIES "Detect dependencies for .deb packages" OFF)

# if we are going to be looking for things, make sure we have the utilities
if(DEB_DETECT_DEPENDENCIES)
	FIND_PROGRAM(DPKG_CMD dpkg)
	FIND_PROGRAM(DPKGQ_CMD dpkg-query)
endif()

# if we want to, and have the capabilities find what is needed,
# based on what backends are turned on and what libraries are installed
if(DEB_DETECT_DEPENDENCIES AND DPKG_CMD AND DPKGQ_CMD)
	message(STATUS "querying installed packages on system for dependancies")
	execute_process(COMMAND "${DPKG_CMD}" --print-architecture 
		OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
		OUTPUT_STRIP_TRAILING_WHITESPACE)
	# don't add a package dependancy if it is not installed locally
	# these should be the debian package names
	set(PACKAGES "libc6")
	if (WITH_LOCAL_BACKEND)
		set(PACKAGES "${PACKAGES} libaio")
	endif()
	if(HAVE_AVAHI)
		set(PACKAGES "${PACKAGES} libavahi-client libavahi-common")
	endif()
	if(WITH_USB_BACKEND)
		set(PACKAGES "${PACKAGES} libusb-1")
	endif()
	if(WITH_XML_BACKEND)
		set(PACKAGES "${PACKAGES} libxml2")
	endif()
	# find the version of the installed package, which is hard to do in
	# cmake first, turn the list into an list (seperated by semicolons)
	string(REGEX REPLACE " " ";" PACKAGES ${PACKAGES})
	# then iterate over each
	foreach(package ${PACKAGES})
		# List packages matching given pattern ${package},
		# key is the glob (*) at the end of the ${package} name,
		# so we don't need to be so specific
		execute_process(COMMAND "${DPKG_CMD}" -l ${package}*
			OUTPUT_VARIABLE DPKG_PACKAGES)
		# returns a string, in a format:
		# ii  libxml2:amd64  2.9.4+dfsg1- amd64 GNOME XML library
		# 'ii' means installed - which is what we are looking for
		STRING(REGEX MATCHALL "ii  ${package}[a-z0-9A-Z.-]*"
				DPKG_INSTALLED_PACKAGES ${DPKG_PACKAGES})
		# get rid of the 'ii', and split things up, so we can look
		# at the name
		STRING(REGEX REPLACE "ii  " ";" NAME_INSTALLED_PACKAGES
			${DPKG_INSTALLED_PACKAGES})
		foreach(match ${NAME_INSTALLED_PACKAGES})
			# ignore packages marked as dev, debug,
			# documentations, or utils
			STRING(REGEX MATCHALL "dev|dbg|doc|utils" TEMP_TEST
					${match})
			if("${TEMP_TEST}" STREQUAL "")
				# find the actual version, executes:
				# dpkg-query --showformat='\${Version}'
			        #	--show libusb-1.0-0
				execute_process(COMMAND "${DPKGQ_CMD}"
				       		 --showformat='\${Version}'
						 --show "${match}"
						 OUTPUT_VARIABLE DPKGQ_VER)
				# debian standard is package_ver-debian_ver,
			        #	"'2.9.4+dfsg1-2.1'"
				# ignore patches and debian suffix version, and
			        #	remove single quote
				string(REGEX REPLACE "[+-][a-z0-9A-Z.]*" ""
						DPKGQ_VER ${DPKGQ_VER})
				string(REGEX REPLACE "'" "" DPKGQ_VER
						${DPKGQ_VER})
				# build the string for the Debian dependancy
				set(CPACK_DEBIAN_PACKAGE_DEPENDS
					"${CPACK_DEBIAN_PACKAGE_DEPENDS}"
					"${match} (>= ${DPKGQ_VER}), ")
			endif()
		endforeach(match)
	endforeach(package)
	# remove the dangling end comma
	string(REGEX REPLACE ", $" "" CPACK_DEBIAN_PACKAGE_DEPENDS
		${CPACK_DEBIAN_PACKAGE_DEPENDS})
else()
	# assume everything is turned on, and running on a modern OS
	set(CPACK_DEBIAN_PACKAGE_DEPENDS "libaio-dev (>= 0.3.109), libavahi-client-dev (>= 0.6.31), libavahi-common-dev (>= 0.6.31), libc6-dev (>= 2.19), libusb-1.0-0-dev (>= 2:1.0.17), libxml2-dev (>= 2.9.1)")
	message(STATUS "Using default dependencies for packaging")
endif()

message(STATUS "Package dependencies: " ${CPACK_DEBIAN_PACKAGE_DEPENDS})

include(CPack)
