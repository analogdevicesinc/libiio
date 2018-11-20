#!/bin/sh -xe

LIBNAME="$1"
TRAVIS_CI="$2"

apt-get -qq update
apt-get -y install sudo

/$LIBNAME/CI/travis/before_install_linux default

# Check we're in Travis-CI; the only place where this context is valid
# It makes sure, users won't shoot themselves in the foot while running this script
if [ "$TRAVIS_CI" == "travis-ci" ] ; then
	mkdir -p /$LIBNAME/build
	cd /$LIBNAME/build
else
	mkdir -p build
	cd build
fi

cmake -DENABLE_PACKAGING=ON -DDEB_DETECT_DEPENDENCIES=ON ..
make
make package

# need to find this out inside the container
. /${LIBNAME}/CI/travis/get_ldist
echo "$(get_ldist)" > /${LIBNAME}/build/.LDIST
