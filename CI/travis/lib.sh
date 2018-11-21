#!/bin/sh -xe

pipeline_branch() {
	local branch=$1

	[ -n "$branch" ] || return 1

	# master is a always a pipeline branch
	[ "$branch" = "master" ] && return 0

	# Check if branch name is 20XX_RY where:
	#   XX - 14 to 99 /* wooh, that's a lot of years */
	#   Y  - 1 to 9   /* wooh, that's a lot of releases per year */
	for year in $(seq 2014 2099) ; do
		for rel_num in $(seq 1 9) ; do
			[ "$branch" = "${year}_R${rel_num}" ] && \
				return 0
		done
	done

	return 1
}

is_deployable_travis_ci_run() {
	# Don't deploy on a Coverity build
	[ -z "${COVERITY_SCAN_PROJECT_NAME}" ] || return 1

	# If we don't have SSH keys, don't bother
	[ -n "${encrypted_48a720578612_key}" ] || return 1
	[ -n "${encrypted_48a720578612_iv}" ] || return 1

	# Don't deploy PRs
	if [ -n "$TRAVIS_PULL_REQUEST" ] && [ "$TRAVIS_PULL_REQUEST" != "false" ] ; then
		return 1
	fi
	# This includes master & release branches [e.g. 2018_R1]
	pipeline_branch "$TRAVIS_BRANCH"
}
