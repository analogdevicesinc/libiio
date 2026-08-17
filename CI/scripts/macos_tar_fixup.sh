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

deps_dir=Library/Frameworks/iio.framework/Versions/Current/Dependencies
libiio_loc=Library/Frameworks/iio.framework/Versions/Current/iio
libiioheader_loc=Library/Frameworks/iio.framework/Versions/Current/Headers/iio.h

mkdir -p "${deps_dir}"

# Create links to framework files
mkdir -p usr/local/{lib,include}
ln -fs "../../../${libiio_loc}" usr/local/lib/libiio.dylib
ln -fs "../../../${libiioheader_loc}" usr/local/include/iio.h

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
for tool in Library/Frameworks/iio.framework/Tools/*;
do
        install_name_tool -add_rpath @loader_path/../.. "${tool}"
done

# Remove old tar and create new one
rm "../${tarname}"
tar -czf "../${tarname}" .
cd ..
rm -rf temp
