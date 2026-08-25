#!/bin/bash -e

release_artifacts() {
        local deb_linux_assets='Fedora-34 Fedora-28 Ubuntu-20.04 Ubuntu-22.04 Ubuntu-24.04 Ubuntu-26.04 Debian-11 Debian-12 openSUSE-15.4'
        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
        for i in $deb_linux_assets; do
                cd "Linux-${i}"
		if [ "${i}" == "Fedora-34" ] || [ "${i}" == "Fedora-28" ]; then
			find . -name '*.rpm' -exec mv {} ../ ";"
		fi
                find . -name '*.deb' -exec mv {} ../ ";"
		find . -name '*.tar.gz' -exec mv {} ../ ";"
                cd ../
                rm -r "Linux-${i}"
        done

	local pkg_assets='macOS-14-arm64 macOS-15-arm64 macOS-latest-arm64'
        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
        for i in $pkg_assets; do
                cd "${i}"

		# All three macOS jobs produce an identically named .pkg, so tag
		# each one with its artifact name before they are moved into a
		# shared directory. Derive the new name from the file itself: byte
		# offsets silently break whenever the version or the abbreviated
		# git hash changes length.
		find . -name '*.pkg' | while IFS= read -r pkg; do
			mv "${pkg}" "${pkg%.pkg}-${i}.pkg"
		done

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
	# The public headers are installed under iio/, so ship the whole
	# directory and keep the <iio/iio.h> include layout inside the zip.
	cp -r ./Windows-VS-2022-x64/iio ./Windows/include/
        cp ./Windows-VS-2022-x64/Windows-msvc-deps.zip ./
        for i in $zip_assets; do
                if [ "${i}" != "MinGW-W64" ]; then
                        rm ./"Windows-${i}"/Windows-msvc-deps.zip
                fi
		rm -r ./"Windows-${i}"/iio
		mv ./"Windows-${i}" Windows
        done
	cp "${BUILD_SOURCESDIRECTORY}/CI/scripts/README.txt" ./Windows
	cd Windows
	zip -r Windows.zip ./*
	cp ./Windows.zip ../
	cd ..
	rm -r Windows

        local deb_arm_assets='arm32v7 arm64v8 ppc64le x390x'
        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"
        for i in $deb_arm_assets; do
                cd "Ubuntu-${i}"
                find . -name '*.deb' -exec mv {} ../ ";"
		find . -name '*.tar.gz' -exec mv {} ../ ";"
                cd ../
                rm -r "Ubuntu-${i}"
        done

        cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}/Debian12-arm"
        find . -name '*.deb' -exec mv {} ../ ";"
        find . -name '*.tar.gz' -exec mv {} ../ ";"
        rm -r ../Debian12-arm

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
