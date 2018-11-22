#!/bin/sh -xe

is_deployable_travis_ci_run() {
	# Don't deploy on a Coverity build
	[ -z "${COVERITY_SCAN_PROJECT_NAME}" ] || return 1

	# If we don't have SSH keys, don't bother
	[ -n "${ENCRYPTED_KEY}" ] || return 1
	[ -n "${ENCRYPTED_IV}" ] || return 1
}

get_ldist() {
	case "$(uname)" in
	Linux*)
		if [ ! -f /etc/os-release ] ; then
			if [ -f /etc/centos-release ] ; then
				echo "centos-$(sed -e 's/CentOS release //' -e 's/(.*)$//' \
					-e 's/ //g' /etc/centos-release)-$(uname -m)"
				return 0
			fi
			ls /etc/*elease
			[ -z "${OSTYPE}" ] || {
				echo "${OSTYPE}-unknown"
				return 0
			}
			echo "linux-unknown"
			return 0
		fi
		. /etc/os-release
		if ! command dpkg --version >/dev/null 2>&1 ; then
			echo $ID-$VERSION_ID-$(uname -m)
		else
			echo $ID-$VERSION_ID-$(dpkg --print-architecture)
		fi
		;;
	Darwin*)
		echo "darwin-$(sw_vers -productVersion)"
		;;
	*)
		echo "$(uname)-unknown"
		;;
	esac
	return 0
}

__brew_install_or_upgrade() {
	brew install $1 || \
		brew upgrade $1 || \
		brew ls --version $1
}

brew_install_or_upgrade() {
	while [ -n "$1" ] ; do
		__brew_install_or_upgrade "$1" || return 1
		shift
	done
}

upload_file_to_swdownloads() {
	if [ "$#" -ne 3 ] ; then
		echo "skipping deployment of something"
		echo "send called with $@"
		return 1
	fi

	if [ "x$1" = "x" ] ; then
		echo no file to send
		return 1
	fi

	if [ ! -r "$1" ] ; then
		echo "file $1 is not readable"
		ls -l $1
		return 1
	fi

	if [ -n "$TRAVIS_PULL_REQUEST_BRANCH" ] ; then
		local branch=$TRAVIS_PULL_REQUEST_BRANCH
	else
		local branch=$TRAVIS_BRANCH
	fi

	# Temporarily disable tracing from here
	set +x

	local FROM=$1
	local TO=${branch}_$2
	local LATE=${branch}_latest_libiio${LDIST}$3
	local GLOB=${DEPLOY_TO}/${branch}_libiio-*

	echo attemting to deploy $FROM to $TO
	echo and ${branch}_libiio${LDIST}$3
	ssh -V

	echo "cd ${DEPLOY_TO}" > script$3
	if curl -m 10 -s -I -f -o /dev/null http://swdownloads.analog.com/cse/travis_builds/${TO} ; then
		echo "rm ${TO}" >> script$3
	fi
	echo "put ${FROM} ${TO}" >> script$3
	echo "ls -l ${TO}" >> script$3
	if curl -m 10 -s -I -f -o /dev/null http://swdownloads.analog.com/cse/travis_builds/${LATE} ; then
		echo "rm ${LATE}" >> script$3
	fi
	echo "symlink ${TO} ${LATE}" >> script$3
	echo "ls -l ${LATE}" >> script$3
	echo "bye" >> script$3

	sftp ${EXTRA_SSH} -b script$3 ${SSHUSER}@${SSHHOST}

	# limit things to a few files, so things don't grow forever
	if [ "$3" = ".deb" ] ; then
		for files in $(ssh ${EXTRA_SSH} ${SSHUSER}@${SSHHOST} \
			"ls -lt ${GLOB}" | tail -n +100 | awk '{print $NF}')
		do
			ssh ${EXTRA_SSH} ${SSHUSER}@${SSHHOST} \
				"rm ${DEPLOY_TO}/${files}"
		done
	fi

	# Re-enable tracing
	set -x
}
