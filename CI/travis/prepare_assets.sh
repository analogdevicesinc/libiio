#!/bin/bash -e

move_artifacts() {
	local rpm_assets='CentOS-7-x86_64 CentOS-8-x86_64'
	cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
	for i in $rpm_assets; do
		cd "${i}"
		find . -name '*.rpm' -exec mv {} ../ ";"
		cd ../
		rm -r "${i}"
	done

        local deb_assets='Ubuntu-16.04-x86_64 Ubuntu-18.04-x86_64
				Ubuntu-20.04-x86_64 Debian-Buster-ARM
				Debian-Buster-ARM64'
        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
        for i in $deb_assets; do
                cd "${i}"
                find . -name '*.deb' -exec mv {} ../ ";"
                cd ../
                rm -r "${i}"
        done

	local pkg_assets='macOS-10.14 macOS-10.15'
        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
        for i in $pkg_assets; do
                cd "${i}"
                find . -name '*.pkg' -exec mv {} ../ ";"
		find . -name '*.gz' -exec mv {} ../ ";"
                cd ../
                rm -r "${i}"
        done
}

archive_windows() {
        local windows_dist='Win32 x64'

        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
        for distribution in $windows_dist; do
		zip -r Windows-VS-16-2019-"${distribution}".zip Windows-VS-16-2019-"${distribution}"
		rm -r Windows-VS-16-2019-"${distribution}"
        done
}

move_artifacts
archive_windows
