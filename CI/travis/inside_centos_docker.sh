#!/bin/sh -xe

LIBNAME="$1"
OS_VERSION="$2"

cd /$LIBNAME

/$LIBNAME/CI/travis/before_install_linux centos

# FIXME: cmake in CentOS 7 is broken, so we need to patch it for now
# Remove this when it's fixed; but make sure we're doing this in Travis-CI context
if [ "$OS_VERSION" == "7" ] ; then
	cd /
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
	cd /$LIBNAME
fi

/$LIBNAME/CI/travis/make_linux centos

# need to find this out inside the container
. /${LIBNAME}/CI/travis/lib.sh
echo "$(get_ldist)" > /${LIBNAME}/build/.LDIST
