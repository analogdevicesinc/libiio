#!/bin/sh -xe

OS_VERSION="$1"
TRAVIS_CI="$2"

# FIXME: see about adding `libserialport-dev` from EPEL ; maybe libusb-1.0.0-devel...
yum -y groupinstall 'Development Tools'
yum -y install cmake libxml2-devel libusb1-devel doxygen libaio-devel \
	avahi-devel bzip2 gzip rpm rpm-build

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
