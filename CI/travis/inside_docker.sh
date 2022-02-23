#!/bin/sh -e

export INSIDE_DOCKER="1"

INSIDE_DOCKER_BUILD_DIR=/docker_build_dir

export TRAVIS_BUILD_DIR="$INSIDE_DOCKER_BUILD_DIR"

cd "$INSIDE_DOCKER_BUILD_DIR"

if [ -d "/$INSIDE_DOCKER_BUILD_DIR/CI" ] ; then
	CI="/$INSIDE_DOCKER_BUILD_DIR/CI"
elif [ -d "/$INSIDE_DOCKER_BUILD_DIR/ci" ] ; then
	CI="/$INSIDE_DOCKER_BUILD_DIR/ci"
else
	echo "No CI/ci directory present"
	exit 1
fi

if [ -f "$INSIDE_DOCKER_BUILD_DIR/inside-travis-ci-docker-env" ] ; then
	. "$INSIDE_DOCKER_BUILD_DIR/inside-travis-ci-docker-env"
fi

"$CI/travis/before_install_linux"

"$CI/travis/make_linux"

# need to find this out inside the container
. "$CI/travis/lib.sh"
echo "$(get_ldist)" > "${INSIDE_DOCKER_BUILD_DIR}/build/.LDIST"
