#!/bin/sh -xe

LIBNAME="$1"
OS_VERSION="$2"

cd /$LIBNAME

# FIXME: see about adding `libserialport-dev` from EPEL ; maybe libusb-1.0.0-devel...
yum -y groupinstall 'Development Tools'
yum -y install cmake libxml2-devel libusb1-devel doxygen libaio-devel \
	avahi-devel bzip2 gzip rpm rpm-build

# FIXME: cmake in CentOS 7 is broken, so we need to patch it for now
# Remove this when it's fixed; but make sure we're doing this in Travis-CI context
if [ "$OS_VERSION" == "7" ] ; then
	patch -p1 <<-EOF
--- a/usr/share/cmake/Modules/CPackRPM.cmake
+++ b/usr/share/cmake/Modules/CPackRPM.cmake
@@ -703,7 +703,7 @@ if(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST)
    message("CPackRPM:Debug: CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST= \${CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST}")
  endif()
   foreach(_DIR \${CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST})
-    list(APPEND _RPM_DIRS_TO_OMIT "-o;-path;.\${_DIR}")
+    list(APPEND _RPM_DIRS_TO_OMIT "-o -path \${_DIR}")
   endforeach()
 endif()
 if (CPACK_RPM_PACKAGE_DEBUG)
	EOF
fi

cmake -DENABLE_PACKAGING=ON ..
make
make package

if [ "$TRAVIS_CI" == "travis-ci" ] ; then
	yum -y install /${LIBNAME}/build/${LIBNAME}-*.rpm
	# need to find this out inside the container
	. /${LIBNAME}/CI/travis/lib.sh
	echo "$(get_ldist)" > /${LIBNAME}/build/.LDIST
fi
