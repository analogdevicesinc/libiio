#!/bin/sh -xe

is_deployable_travis_ci_run() {
	# Don't deploy on a Coverity build
	[ -z "${COVERITY_SCAN_PROJECT_NAME}" ] || return 1

	# If we don't have SSH keys, don't bother
	[ -n "${ENCRYPTED_KEY}" ] || return 1
	[ -n "${ENCRYPTED_IV}" ] || return 1
}
