#!/bin/sh -xe

OS_VERSION="$1"
TRAVIS_CI="$2"

# FIXME: see about adding `libserialport-dev` from EPEL ; maybe libusb-1.0.0-devel...
yum -y groupinstall 'Development Tools'
yum -y install cmake libxml2-devel libusb1-devel doxygen libaio-devel \
	avahi-devel bzip2 gzip rpm rpm-build

# FIXME: cmake in CentOS 7 is broken, so we need to patch it for now
# Remove this when it's fixed; but make sure we're doing this in Travis-CI context
if [ "$TRAVIS_CI" == "travis-ci" ] ; then
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
fi

# Check we're in Travis-CI; the only place where this context is valid
# It makes sure, users won't shoot themselves in the foot while running this script
if [ "$TRAVIS_CI" == "travis-ci" ] ; then
	mkdir -p /libiio/build
	cd /libiio/build
else
	mkdir -p build
	cd build
fi

cmake -DENABLE_PACKAGING=ON ..
make
make package

if [ "$TRAVIS_CI" == "travis-ci" ] ; then
	yum -y install /libiio/build/libiio-*.rpm
fi
