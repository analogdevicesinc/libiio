#!/bin/bash -e

release_artifacts() {
        local deb_linux_assets='Fedora-42 Fedora-44 Ubuntu-22.04 Ubuntu-24.04 Ubuntu-26.04 Debian-12 Debian-13 openSUSE-15.6 openSUSE-16.0'
        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
        for i in $deb_linux_assets; do
                cd "Linux-${i}"
		if [[ "${i}" == Fedora-* ]]; then
			find . -name '*.rpm' -exec mv {} ../ ";"
		fi
                find . -name '*.deb' -exec mv {} ../ ";"
		find . -name '*.tar.gz' -exec mv {} ../ ";"
                cd ../
                rm -r "Linux-${i}"
        done

	local pkg_assets='macOS-15-arm64 macOS-15-x64 macOS-26-arm64 macOS-26-x64 macOS-27-arm64'
        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
        for i in $pkg_assets; do
                cd "${i}"

		# change artifact name
		old_name=$(find . -name '*.pkg' | cut -b 3-26)
		name=$(echo "${old_name}" | cut -b 1-20)
		new_name="${name}-${i}.pkg"
		mv ./"${old_name}" ./"${new_name}"

                find . -name '*.pkg' -exec mv {} ../ ";"
		find . -name '*.tar.gz' -exec mv {} ../ ";"
                cd ../
                rm -r "${i}"
        done

        local zip_assets='VS-2022-x64 MinGW-W64'
        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
	mkdir Windows
	cd Windows
	mkdir include
	cd ..
	cp ./Windows-VS-2022-x64/iio.h ./Windows/include
        for i in $zip_assets; do
		rm ./"Windows-${i}"/iio.h
		mv ./"Windows-${i}" Windows
        done
	cp "${BUILD_SOURCESDIRECTORY}/CI/azure/README.txt" ./Windows
	cd Windows
	zip -r Windows.zip ./*
	cp ./Windows.zip ../
	cd ..
	rm -r Windows

        local deb_arm_assets='Ubuntu-22.04-arm32v7 Ubuntu-22.04-arm64v8 Ubuntu-22.04-ppc64le Ubuntu-22.04-s390x Ubuntu-26.04-arm32v7 Ubuntu-26.04-arm64v8 Ubuntu-26.04-ppc64le Ubuntu-26.04-s390x Debian-12-arm64 Debian-12-armhf Debian-13-arm64 Debian-13-armhf'
        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
        for i in $deb_arm_assets; do
                cd "${i}"
                find . -name '*.deb' -exec mv {} ../ ";"
		find . -name '*.tar.gz' -exec mv {} ../ ";"
                cd ../
                rm -r "${i}"
        done

}

check_artifacts() {
	cd build
	while IFS= read -r line; do
		if [ -z "${line}" ]; then continue
		fi
		test -f ./artifacts/"${line}" && echo "${line} exist." || echo "${line} does not exist."
	done < "artifact_manifest.txt"
}

"${1}"_artifacts
