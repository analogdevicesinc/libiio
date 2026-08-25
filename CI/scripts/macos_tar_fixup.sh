#!/bin/bash -xe

# Extract tar.gz to temp folder
tarname=$(find . -maxdepth 1 -name '*.tar.gz')
if [ -z "${tarname}" ]; then
        echo "tar.gz not found"
        exit 1
fi
# Remove .tar.gz from filename
subfoldername=$(echo "${tarname}" | rev | cut -b 8- | rev)

mkdir -p temp_tar
tar -xzf "${tarname}" -C temp_tar
mv "temp_tar/${subfoldername}" temp
cd temp

# Find the framework — it may be at Library/Frameworks/ or usr/Library/Frameworks/
# depending on the install prefix and CMake version.
fw_base=$(find . -path "*/iio.framework/Versions" -type d | head -1)
if [ -z "${fw_base}" ]; then
	echo "iio.framework/Versions not found in tar"
	exit 1
fi
fw_versions="${fw_base}"
fw_dir=$(dirname "${fw_base}")
fw_version=$(ls "${fw_versions}" | grep -v Current | sort -V | tail -1)
if [ -z "${fw_version}" ]; then
	echo "No version directory found in ${fw_versions}"
	exit 1
fi

# Create the Current symlink if it doesn't exist
if [ ! -e "${fw_versions}/Current" ]; then
	ln -s "${fw_version}" "${fw_versions}/Current"
fi

deps_dir="${fw_versions}/${fw_version}/Dependencies"
libiio_loc="${fw_versions}/${fw_version}/iio"
libiioheader_loc="${fw_versions}/${fw_version}/Headers/iio.h"

mkdir -p "${deps_dir}"

# Create links to framework files
mkdir -p usr/local/{lib,include}
ln -fs "$(python3 -c "import os; print(os.path.relpath('${libiio_loc}', 'usr/local/lib'))")" usr/local/lib/libiio.dylib
ln -fs "$(python3 -c "import os; print(os.path.relpath('${libiioheader_loc}', 'usr/local/include'))")" usr/local/include/iio.h

# Update rpath of library
install_name_tool -add_rpath @loader_path/. "${libiio_loc}"

# Copy dependent libs to local libs, and update rpath of dependencies
for each in $(otool -L "${libiio_loc}" |grep '\/usr\/local\|homebrew' |cut -f2 | cut -d' ' -f1) ; do
	name=$(basename "${each}")
	cp "${each}" "${deps_dir}"
	chmod +w "${deps_dir}/${name}"
	install_name_tool -id "@rpath/Dependencies/${name}" "${deps_dir}/${name}"
	install_name_tool -change "${each}" "@rpath/Dependencies/${name}" "${libiio_loc}"
	codesign --force -s - "${deps_dir}/${name}"
done

# Update tools
for tool in "${fw_dir}"/Tools/*;
do
        install_name_tool -add_rpath @loader_path/../.. "${tool}"
done

# Remove old tar and create new one
rm "../${tarname}"
tar -czf "../${tarname}" .
cd ..
rm -rf temp
