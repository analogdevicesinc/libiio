# support creating some basic binpkgs via `make package`
set(CPACK_SET_DESTDIR ON)
set(CPACK_GENERATOR TGZ)

# Top level settings
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)
set(CPACK_PACKAGE_VERSION_MAJOR ${LIBIIO_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${LIBIIO_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH g${LIBIIO_VERSION_GIT})
set(CPACK_BUNDLE_NAME libiio)
set(CPACK_PACKAGE_VERSION ${LIBIIO_VERSION})

# Start with empty file
set(REQUIRES "${CMAKE_BINARY_DIR}/require_manifest.txt")
file(WRITE ${REQUIRES} "")

# Determine the distribution we are on
file(STRINGS /etc/os-release distro REGEX "^NAME=")
string(REGEX REPLACE "NAME=\"(.*)\"" "\\1" distro "${distro}")
file(STRINGS /etc/os-release disversion REGEX "^VERSION_ID=")
string(REGEX REPLACE "VERSION_ID=\"(.*)\"" "\\1" disversion "${disversion}")

if(distro MATCHES "\\.*Ubuntu\\.*")
	set(CPACK_GENERATOR ${CPACK_GENERATOR};DEB)
elseif (distro MATCHES "\\.*Debian\\.*")
	set(CPACK_GENERATOR ${CPACK_GENERATOR};DEB)
elseif (distro MATCHES "\\.*Fedora\\.*")
	set(CPACK_GENERATOR ${CPACK_GENERATOR};RPM)
elseif (distro MATCHES "\\.*CentOS\\.*")
	set(CPACK_GENERATOR ${CPACK_GENERATOR};RPM)
elseif (distro MATCHES "\\.*SUSE\\.*")
	set(CPACK_GENERATOR ${CPACK_GENERATOR};RPM)
else()
	message(STATUS "found unknown distribution ${distro}, Version : ${disversion}")
	message(FATAL_ERROR "please report an error to https:\\/\\/github.com\\/analogdevicesinc\\/libiio\\/issues")
endif()

if (CPACK_GENERATOR MATCHES "RPM")
	FIND_PROGRAM(RPMBUILD rpmbuild)
	FIND_PROGRAM(RPM_CMD rpm)
	set(CPACK_PACKAGE_RELOCATABLE OFF)
	if (NOT RPMBUILD)
		message (FATAL_ERROR "To build packages, please install rpmbuild")
	elseif(NOT RPM_CMD)
		message(STATUS "assuming default package versions")
	else()
		message(STATUS "querying ${RPM_CMD} for package dependencies")
	endif()

	# Add these for CentOS 7
	set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
		/lib
		/lib/udev
		/lib/udev/rules.d
		/usr/sbin
		/usr/lib/python2.7
		/usr/lib/python2.7/site-packages
		/usr/lib/pkgconfig
		/usr/lib64/pkgconfig
	)
endif()

# if we are going to be looking for things, make sure we have the utilities
if(CPACK_GENERATOR MATCHES "DEB")
	FIND_PROGRAM(DPKG_CMD dpkg)
	FIND_PROGRAM(DPKGQ_CMD dpkg-query)
	if (NOT DPKG_CMD)
		message (FATAL_ERROR "To build packages, please install dpkg")
	elseif (NOT DPKGQ_CMD)
		message(STATUS "assuming default package versions")
	else()
		message(STATUS "querying ${DPKGQ_CMD} for package dependencies")
	endif()
	# debian specific package settings
	set(CPACK_PACKAGE_CONTACT "Engineerzone <https://ez.analog.com/community/linux-device-drivers>")
endif()

if(NOT CMAKE_CROSSCOMPILING)
	 execute_process(COMMAND "${DPKG_CMD}" --print-architecture
		OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
		OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
	message(FATAL_ERROR "package building with cross compile not handled - please -DENABLE_PACKAGING=OFF")
endif()

# don't add a package dependency if it is not installed locally
# these should be the debian package names
if (CPACK_GENERATOR MATCHES "DEB")
	set(PACKAGES "libc6")
	set(DEFAULT_DEB "libc6 (>= 2.17), ")
elseif(CPACK_GENERATOR MATCHES "RPM")
	set(PACKAGES "glibc")
	set(DEFAULT_RPM "glibc >=2.17, ")
endif()

if (WITH_ZSTD)
	set(PACKAGES "${PACKAGES} libzstd")
	if (CPACK_GENERATOR MATCHES "DEB")
		set(DEFAULT_DEB "${DEFAULT_DEB}libzstd (>= 1.5.0), ")
	elseif (CPACK_GENERATOR MATCHES "RPM")
		set(DEFAULT_RPM "${DEFAULT_RPM}libzstd >= 1.5.0, ")
	endif()
endif()
# libpthread.so is provided by libc6, so don't check NEED_THREADS
if (WITH_LOCAL_BACKEND)
	set(PACKAGES "${PACKAGES} libaio")
	if (CPACK_GENERATOR MATCHES "DEB")
		set(DEFAULT_DEB "${DEFAULT_DEB}libaio (>= 0.3.109), ")
	elseif(CPACK_GENERATOR MATCHES "RPM")
		set(DEFAULT_RPM "${DEFAULT_RPM}libaio >=0.3.109, ")
	endif()
	# librt.so is provided by libc6 which is already included
endif()
if(HAVE_AVAHI)
	if (CPACK_GENERATOR MATCHES "DEB")
		set(PACKAGES "${PACKAGES} libavahi-client libavahi-common")
		set(DEFAULT_DEB "${DEFAULT_DEB}libavahi-client3 (>= 0.6.31), libavahi-common3 (>= 0.6.31), ")
	elseif(CPACK_GENERATOR MATCHES "RPM")
		set(PACKAGES "${PACKAGES} avahi")
		set(DEFAULT_RPM "${DEFAULT_RPM}avahi >= 0.6.31, avahi-libs >= 0.6.31, ")
	endif()
endif()
if(WITH_USB_BACKEND)
	if (CPACK_GENERATOR MATCHES "DEB")
		set(PACKAGES "${PACKAGES} libusb-1")
		set(DEFAULT_DEB "${DEFAULT_DEB}libusb-1 (<2:1.0.5), ")
	elseif(CPACK_GENERATOR MATCHES "RPM")
		set(PACKAGES "${PACKAGES} libusb")
		set(DEFAULT_RPM "${DEFAULT_RPM}libuse>= 0.1.5, ")
	endif()
endif()
if(WITH_XML_BACKEND)
	set(PACKAGES "${PACKAGES} libxml2")
	if (CPACK_GENERATOR MATCHES "DEB")
		 set(DEFAULT_DEB "${DEFAULT_DEB}libxml2 (>= 2.9.4), ")
	 elseif (CPACK_GENERATOR MATCHES "RPM")
		 set(DEFAULT_RPM "${DEFAULT_RPM}libxml2 >= 2.9.4, ")
	 endif()
endif()
if(WITH_SERIAL_BACKEND)
	if (CPACK_GENERATOR MATCHES "DEB")
		set(PACKAGES "${PACKAGES} libserialport0")
		set(DEFAULT_DEB "${DEFAULT_DEB}libserialport0 (>= 0.1.1), ")
	else()
		set(PACKAGES "${PACKAGES} libserialport")
		set(DEFAULT_RPM "${DEFAULT_RPM}libserialport >= 0.1.1, ")
	endif()
endif()

# find the version of the installed package, which is hard to do in
# cmake first, turn the list into a list (separated by semicolons)
string(REGEX REPLACE " " ";" PACKAGES ${PACKAGES})
# then iterate over each
foreach(package ${PACKAGES})
	# List packages matching given pattern ${package},
	# key is the glob (*) at the end of the ${package} name,
	# so we don't need to be so specific
	if (CPACK_GENERATOR MATCHES "DEB" AND DPKG_CMD)
		execute_process(COMMAND "${DPKG_CMD}" -l ${package}*
			OUTPUT_VARIABLE DPKG_PACKAGES)
		# returns a string, in a format:
		# ii  libxml2:amd64  2.9.4+dfsg1- amd64 GNOME XML library
		# 'ii' means installed - which is what we are looking for
		STRING(REGEX MATCHALL "ii  ${package}[a-z0-9A-Z.-]*:${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}"
				DPKG_INSTALLED_PACKAGES ${DPKG_PACKAGES})
		# get rid of the 'ii', and split things up, so we can look
		# at the name
		STRING(REGEX REPLACE "ii  " ";" NAME_INSTALLED_PACKAGES
			${DPKG_INSTALLED_PACKAGES})
	elseif (CPACK_GENERATOR MATCHES "RPM" AND RPM_CMD)
		execute_process(COMMAND "${RPM_CMD}" -qa --qf "%{n};" "${package}*"
			OUTPUT_VARIABLE NAME_INSTALLED_PACKAGES)
	else()
		set(NAME_INSTALLED_PACKAGES ${package})
	endif()
	list(REMOVE_DUPLICATES NAME_INSTALLED_PACKAGES)
	string(STRIP "${NAME_INSTALLED_PACKAGES}" NAME_INSTALLED_PACKAGES)
	if (NAME_INSTALLED_PACKAGES STREQUAL "")
		message(FATAL_ERROR "could not find ${package} installed")
	endif()
	foreach(match ${NAME_INSTALLED_PACKAGES})
		# ignore packages marked as dev, debug,
		# documentations, utils, data or cross
		STRING(REGEX MATCHALL "-dev|-dbg|-doc|-lang|-extra|-data|-utils|-cross|-plugins|-python|-headers|-gobject|-locale|-tools|glibc-common|libusbmuxd|libusbx" TEMP_TEST
				${match})
		if(NOT "${TEMP_TEST}" STREQUAL "")
			continue()
		endif()
		if (CPACK_GENERATOR MATCHES "DEB" AND DPKGQ_CMD)
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
			string(REGEX REPLACE "[+-][a-z0-9A-Z.]*" "" DPKGQ_VER ${DPKGQ_VER})
			string(REGEX REPLACE "'" "" DPKGQ_VER ${DPKGQ_VER})
			# build the string for the Debian dependency
			STRING(REGEX REPLACE ":.*" "" TEMP_TEST ${match})
			set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}"
				"${TEMP_TEST} (>= ${DPKGQ_VER}), ")
		elseif (CPACK_GENERATOR MATCHES "RPM" AND RPM_CMD)
			# find the actual version, executes:
			# rpm -qp --queryformat '%{VERSION}' libusb1
			execute_process(COMMAND "${RPM_CMD}" -q --queryformat '%{VERSION}' ${match}
					OUTPUT_VARIABLE RPM_VER)
			STRING(REGEX REPLACE "'" "" RPM_VER ${RPM_VER})
			set(CPACK_RPM_PACKAGE_REQUIRES "${CPACK_RPM_PACKAGE_REQUIRES}"
				"${match} >= ${RPM_VER}, ")
		endif()
		# find the actual so files
		STRING(REGEX MATCHALL "libc6|glibc" TEMP_TEST ${match})
		if(NOT "${TEMP_TEST}" STREQUAL "")
			continue()
		endif()
		if (CPACK_GENERATOR MATCHES "DEB" AND DPKG_CMD)
			# build up the so locations
			execute_process(COMMAND "${DPKG_CMD}"
				-L "${match}"
				OUTPUT_VARIABLE DPK_RESULT)
		elseif (CPACK_GENERATOR MATCHES "RPM" AND RPM_CMD)
			execute_process(COMMAND "${RPM_CMD}" -ql ${match}
				OUTPUT_VARIABLE DPK_RESULT)
		else()
			continue()
		endif()
		STRING(STRIP ${DPK_RESULT} STRIPPED)
		STRING(REGEX REPLACE "[\r\n]" ";" POSSIBLE_SO "${STRIPPED}")
		foreach(is_so ${POSSIBLE_SO})
			# match with ".so." or ".so" (and the end)
			STRING(REGEX MATCHALL "\\.so$|\\.so\\." TEMP_TEST ${is_so})
			if("${TEMP_TEST}" STREQUAL "")
				continue()
			endif()
			if(IS_SYMLINK ${is_so})
				continue()
			endif()
			file(APPEND ${REQUIRES} "${is_so}\n")
		endforeach(is_so)
	endforeach(match)
endforeach(package)

configure_file( "${CMAKE_SOURCE_DIR}/cmake/add_requirements2tar.sh.in"
		"${CMAKE_BINARY_DIR}/add_requirements2tar.sh"
		IMMEDIATE @ONLY
	)
add_custom_target(required2tar
	COMMAND sh ${CMAKE_BINARY_DIR}/add_requirements2tar.sh
	COMMENT "Adding requirements to tarball"
	)

if (CPACK_GENERATOR MATCHES "DEB")
	if (NOT DPKGQ_CMD)
		set(CPACK_DEBIAN_PACKAGE_DEPENDS "${DEFAULT_DEB}")
	endif()
	# remove the dangling end comma
	string(REGEX REPLACE ", $" "" CPACK_DEBIAN_PACKAGE_DEPENDS
		${CPACK_DEBIAN_PACKAGE_DEPENDS})
	message(STATUS "Dependencies (.deb): " ${CPACK_DEBIAN_PACKAGE_DEPENDS})
elseif (CPACK_GENERATOR MATCHES "RPM")
	if (NOT RPM_CMD)
		set(CPACK_RPM_PACKAGE_REQUIRES "${DEFAULT_RPM}")
	endif()
	string(REGEX REPLACE ", $" "" CPACK_RPM_PACKAGE_REQUIRES
		${CPACK_RPM_PACKAGE_REQUIRES})
	message(STATUS "Dependencies (.rpm): " ${CPACK_RPM_PACKAGE_REQUIRES})
endif()

# Make sure the generated .deb cannot be installed alongside the Debian ones
set(CPACK_DEBIAN_PACKAGE_PROVIDES
	"libiio0 (= ${LIBIIO_VERSION}), libiio-dev (= ${LIBIIO_VERSION}), libiio-utils (= ${LIBIIO_VERSION}), iiod (= ${LIBIIO_VERSION})"
)
set(CPACK_DEBIAN_PACKAGE_CONFLICTS "libiio0, libiio-dev, libiio-utils, iiod")
set(CPACK_DEBIAN_PACKAGE_REPLACES "libiio0, libiio-dev, libiio-utils, iiod")

include(CPack)
