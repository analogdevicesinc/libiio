#!/bin/sh -e

LIBNAME="$1"
OS_TYPE="$2"

export INSIDE_DOCKER="1"
export TRAVIS_BUILD_DIR="/$LIBNAME"

cd /$LIBNAME

if [ -d "/$LIBNAME/CI" ] ; then
	CI="/$LIBNAME/CI"
elif [ -d "/$LIBNAME/ci" ] ; then
	CI="/$LIBNAME/ci"
else
	echo "No CI/ci directory present"
	exit 1
fi

$CI/travis/before_install_linux "$OS_TYPE"

$CI/travis/make_linux "$OS_TYPE"

# need to find this out inside the container
. $CI/travis/lib.sh
echo "$(get_ldist)" > /${LIBNAME}/build/.LDIST
