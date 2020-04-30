#!/bin/sh -e

if [ "$TRIGGER_NEXT_BUILD" = "true" ] && [ "$TRIGGERING_NEXT_BUILD" != "true" ] ; then
	exit 0
fi

export TRAVIS_API_URL="https://api.travis-ci.org"
LOCAL_BUILD_DIR=${LOCAL_BUILD_DIR:-build}

HOMEBREW_NO_INSTALL_CLEANUP=1
export HOMEBREW_NO_INSTALL_CLEANUP

# Add here all the common env-vars that should be propagated
# to the docker image, simply by referencing the env-var name.
# The values will be evaluated.
#
# Make sure to not pass certain stuff that are specific to the host
# and not specific to inside-the-docker (like TRAVIS_BUILD_DIR)
#
# If these nothing should be passed, then clear or
#'unset INSIDE_DOCKER_TRAVIS_CI_ENV' after this script is included
INSIDE_DOCKER_TRAVIS_CI_ENV="TRAVIS TRAVIS_COMMIT TRAVIS_PULL_REQUEST OS_VERSION"

COMMON_SCRIPTS="inside_docker.sh"

echo_red()   { printf "\033[1;31m$*\033[m\n"; }
echo_green() { printf "\033[1;32m$*\033[m\n"; }
echo_blue()  { printf "\033[1;34m$*\033[m\n"; }

get_script_path() {
	local script="$1"

	[ -n "$script" ] || return 1

	if [ -f "CI/travis/$script" ] ; then
		echo "CI/travis/$script"
	elif [ -f "ci/travis/$script" ] ; then
		echo "ci/travis/$script"
	elif [ -f "${LOCAL_BUILD_DIR}/$script" ] ; then
		echo "${LOCAL_BUILD_DIR}/$script"
	else
		return 1
	fi
}

pipeline_branch() {
	local branch=$1

	[ -n "$branch" ] || return 1

	# master is a always a pipeline branch
	[ "$branch" = "master" ] && return 0

	set +x
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

should_trigger_next_builds() {
	local branch="$1"

	[ -z "${COVERITY_SCAN_PROJECT_NAME}" ] || return 1

	# These Travis-CI vars have to be non-empty
	[ -n "$TRAVIS_PULL_REQUEST" ] || return 1
	[ -n "$branch" ] || return 1
	set +x
	[ -n "$TRAVIS_API_TOKEN" ] || return 1

	# Has to be a non-pull-request
	[ "$TRAVIS_PULL_REQUEST" = "false" ] || return 1

	pipeline_branch "$branch" || return 1
}

trigger_build() {
	local repo_slug="$1"
	local branch="$2"

	[ -n "$repo_slug" ] || return 1
	[ -n "$branch" ] || return 1

	local body="{
		\"request\": {
			\"branch\":\"$branch\"
		}
	}"

	# Turn off tracing here (shortly)
	set +x
	curl -s -X POST \
		-H "Content-Type: application/json" \
		-H "Accept: application/json" \
		-H "Travis-API-Version: 3" \
		-H "Authorization: token $TRAVIS_API_TOKEN" \
		-d "$body" \
		"https://api.travis-ci.org/repo/$repo_slug/requests"
}

trigger_adi_build() {
	local adi_repo="$1"
	local branch="$2"

	[ -n "$adi_repo" ] || return 1
	trigger_build "analogdevicesinc%2F$adi_repo" "$branch"
}

command_exists() {
	local cmd=$1
	[ -n "$cmd" ] || return 1
	type "$cmd" >/dev/null 2>&1
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
		if ! command_exists dpkg ; then
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
	brew install "$1" || \
		brew upgrade "$1" || \
		brew ls --versions "$1"
}

brew_install_or_upgrade() {
	while [ -n "$1" ] ; do
		__brew_install_or_upgrade "$1" || return 1
		shift
	done
}

__brew_install_if_not_exists() {
	brew ls --versions "$1" || \
		brew install "$1"
}

brew_install_if_not_exists() {
	while [ -n "$1" ] ; do
		__brew_install_if_not_exists "$1" || return 1
		shift
	done
}

sftp_cmd_pipe() {
	sftp "${EXTRA_SSH}" "${SSHUSER}@${SSHHOST}"
}

sftp_rm_artifact() {
	local artifact="$1"
	sftp_cmd_pipe <<-EOF
		cd ${DEPLOY_TO}
		rm ${artifact}
	EOF
}

sftp_upload() {
	local FROM="$1"
	local TO="$2"
	local LATE="$3"

	sftp_cmd_pipe <<-EOF
		cd ${DEPLOY_TO}

		put ${FROM} ${TO}
		ls -l ${TO}

		symlink ${TO} ${LATE}
		ls -l ${LATE}
		bye
	EOF
}

upload_file_to_swdownloads() {

	if [ "$#" -ne 4 ] ; then
		echo "skipping deployment of something"
		echo "send called with $@"
		return 0
	fi

	local LIBNAME=$1
	local FROM=$2
	local FNAME=$3
	local EXT=$4

	if [ -z "$FROM" ] ; then
		echo no file to send
		return 1
	fi

	if [ ! -r "$FROM" ] ; then
		echo "file $FROM is not readable"
		return 1
	fi

	if [ -n "$TRAVIS_PULL_REQUEST_BRANCH" ] ; then
		local branch="$TRAVIS_PULL_REQUEST_BRANCH"
	else
		local branch="$TRAVIS_BRANCH"
	fi

	local TO=${branch}_${FNAME}
	local LATE=${branch}_latest_${LIBNAME}${LDIST}${EXT}
	local GLOB=${DEPLOY_TO}/${branch}_${LIBNAME}-*

	echo attempting to deploy "$FROM" to "$TO"
	echo and "${branch}_${LIBNAME}${LDIST}${EXT}"
	ssh -V

	for rmf in ${TO} ${LATE} ; do
		sftp_rm_artifact "${rmf}" || \
			echo_blue "Could not delete ${rmf}"
	done

	sftp_upload "${FROM}" "${TO}" "${LATE}" || {
		echo_red "Failed to upload artifact from '${FROM}', to '${TO}', symlink '${LATE}'"
		return 1
	}

	# limit things to a few files, so things don't grow forever
	if [ "${EXT}" = ".deb" ] ; then
		for files in $(ssh "${EXTRA_SSH}" "${SSHUSER}@${SSHHOST}" \
			"ls -lt ${GLOB}" | tail -n +100 | awk '{print $NF}')
		do
			ssh "${EXTRA_SSH}" "${SSHUSER}@${SSHHOST}" \
				"rm ${DEPLOY_TO}/${files}" || \
				return 1
		done
	fi

	return 0
}

prepare_docker_image() {
	local DOCKER_IMAGE="$1"
	sudo apt-get -qq update
	echo 'DOCKER_OPTS="-H tcp://127.0.0.1:2375 -H unix:///var/run/docker.sock -s devicemapper"' | sudo tee /etc/default/docker > /dev/null
	sudo service docker restart
	sudo docker pull "$DOCKER_IMAGE"
}

__save_env_for_docker() {
	local env_file="$1/inside-travis-ci-docker-env"
	for env in $INSIDE_DOCKER_TRAVIS_CI_ENV ; do
		val="$(eval echo "\$${env}")"
		if [ -n "$val" ] ; then
			echo "export ${env}=${val}" >> "${env_file}"
		fi
	done
}

run_docker_script() {
	local DOCKER_SCRIPT="$(get_script_path $1)"
	local DOCKER_IMAGE="$2"
	local OS_TYPE="$3"
	local MOUNTPOINT="${4:-docker_build_dir}"

	__save_env_for_docker "${TRAVIS_BUILD_DIR}"

	sudo docker run --rm=true \
		-v "$(pwd):/${MOUNTPOINT}:rw" \
		"$DOCKER_IMAGE" \
		/bin/bash -e "/${MOUNTPOINT}/${DOCKER_SCRIPT}" "${MOUNTPOINT}" "${OS_TYPE}"
}

ensure_command_exists() {
	local cmd="$1"
	local package="$2"
	[ -n "$cmd" ] || return 1
	[ -n "$package" ] || package="$cmd"
	! command_exists "$cmd" || return 0
	# go through known package managers
	for pacman in apt-get brew yum ; do
		command_exists $pacman || continue
		"$pacman" install -y "$package" || {
			# Try an update if install doesn't work the first time
			"$pacman" -y update && \
				"$pacman" install -y "$package"
		}
		return $?
	done
	return 1
}

version_gt() { test "$(echo "$@" | tr " " "\n" | sort -V | head -n 1)" != "$1"; }
version_le() { test "$(echo "$@" | tr " " "\n" | sort -V | head -n 1)" = "$1"; }
version_lt() { test "$(echo "$@" | tr " " "\n" | sort -rV | head -n 1)" != "$1"; }
version_ge() { test "$(echo "$@" | tr " " "\n" | sort -rV | head -n 1)" = "$1"; }

get_codename() {
	lsb_release -c -s
}

get_dist_id() {
	lsb_release -i -s
}

get_version() {
	lsb_release -r -s
}

is_ubuntu_at_least_ver() {
	[ "$(get_dist_id)" = "Ubuntu" ] || return 1
	version_ge "$(get_version)" "$1"
}

is_centos_at_least_ver() {
	[ "$(get_dist_id)" = "CentOS" ] || return 1
	version_ge "$(get_version)" "$1"
}

print_github_api_rate_limits() {
	# See https://developer.github.com/v3/rate_limit/
	# Note: Accessing this endpoint does not count against your REST API rate limit.
	echo_green '-----------------------------------------'
	echo_green 'Github API Rate limits'
	echo_green '-----------------------------------------'
	wget -q -O- https://api.github.com/rate_limit
	echo_green '-----------------------------------------'
}

ensure_command_exists sudo
ensure_command_exists wget

# Other scripts will download lib.sh [this script] and lib.sh will
# in turn download the other scripts it needs.
# This gives way more flexibility when changing things, as they propagate
for script in $COMMON_SCRIPTS ; do
	[ ! -f "CI/travis/$script" ] || continue
	[ ! -f "ci/travis/$script" ] || continue
	[ ! -f "${LOCAL_BUILD_DIR}/$script" ] || continue
	mkdir -p "${LOCAL_BUILD_DIR}"
	wget https://raw.githubusercontent.com/analogdevicesinc/libiio/master/CI/travis/$script \
		-O "$LOCAL_BUILD_DIR/$script"
done

print_github_api_rate_limits
