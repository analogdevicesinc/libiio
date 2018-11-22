#!/bin/sh -xe

LIBNAME="$1"

cd /$LIBNAME

apt-get -qq update
apt-get -y install sudo

/$LIBNAME/CI/travis/before_install_linux default

/$LIBNAME/CI/travis/make_linux "$LIBNAME" default

# need to find this out inside the container
. /${LIBNAME}/CI/travis/lib.sh
echo "$(get_ldist)" > /${LIBNAME}/build/.LDIST
