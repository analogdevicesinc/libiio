#!/bin/bash -e

release_artifacts() {
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

	local pkg_assets='macOS-10.15 macOS-11'
        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
        for i in $pkg_assets; do
                cd "${i}"
                find . -name '*.pkg' -exec mv {} ../ ";"
		find . -name '*.gz' -exec mv {} ../ ";"
                cd ../
                rm -r "${i}"
        done

        local windows_dist='Win32 x64'
        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
        for distribution in $windows_dist; do
		zip -r Windows-VS-16-2019-"${distribution}".zip Windows-VS-16-2019-"${distribution}"
		rm -r Windows-VS-16-2019-"${distribution}"
        done
}

swdownloads_artifacts() {
        local linux_dist='CentOS-7-x86_64 CentOS-8-x86_64 Ubuntu-16.04-x86_64 Ubuntu-18.04-x86_64
                                Ubuntu-20.04-x86_64 Debian-Buster-ARM Debian-Buster-ARM64'
        for distribution in $linux_dist; do
		cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
		cd "Linux-${distribution}"
                find . -name '*.rpm' -exec mv {} ../"${distribution}"_latest_master_libiio.rpm ";"
                find . -name '*.deb' -exec mv {} ../"${distribution}"_latest_master_libiio.deb ";"
                rm -r ../Linux-"${distribution}"
        done

	local macOS_dist='macOS-10.15 macOS-11'
	for distribution in $macOS_dist; do
                cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
                cd "${distribution}"
                find . -name '*.pkg' -exec mv {} ../"${distribution}"_latest_master_libiio.pkg ";"
                find . -name '*.tar.gz' -exec mv {} ../"${distribution}"_latest_master_libiio.tar.gz ";"
                rm -r ../"${distribution}"
        done

	local windows_dist='Win32 x64'
        for distribution in $windows_dist; do
		cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
                zip -r Windows-VS-16-2019-"${distribution}".zip Windows-VS-16-2019-"${distribution}"
                rm -r Windows-VS-16-2019-"${distribution}"
        done

	cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}/Libiio-Setup-Exe"
	mv libiio-setup.exe ../libiio-setup.exe
	rm -r ../Libiio-Setup-Exe
}

"${1}"_artifacts
