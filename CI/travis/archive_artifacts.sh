#!/bin/bash -e

archive_linux() {
	local linux_dist='CentOS-7-x86_64 CentOS-8-x86_64 Ubuntu-16.04-x86_64
		Ubuntu-18.04-x86_64 Ubuntu-20.04-x86_64 Debian-Buster-ARM Debian-Buster-ARM64'

	cd "${SOURCE_DIRECTORY}"
	for distribution in $linux_dist; do
		tar -zcvf Linux-"${distribution}".tar.gz Linux-"${distribution}"
		rm -r Linux-"${distribution}"
	done
}

archive_macOS() {
        local macOS_dist='10.14 10.15'

        cd "${SOURCE_DIRECTORY}"
        for distribution in $macOS_dist; do
		tar -zcvf macOS-"${distribution}".tar.gz macOS-"${distribution}"
		rm -r macOS-"${distribution}"
        done
}

archive_windows() {
        local windows_dist='Win32 x64'

        cd "${SOURCE_DIRECTORY}"
        for distribution in $windows_dist; do
		zip -r Windows-VS-16-2019-"${distribution}".zip Windows-VS-16-2019-"${distribution}"
		rm -r Windows-VS-16-2019-"${distribution}"
        done
}

archive_linux
archive_macOS
archive_windows
