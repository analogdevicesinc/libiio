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

# Update rpath of library and tools
libusb_loc=$(find $(brew --cellar) -name libusb-1.0.dylib)
libxml_loc=$(find $(brew --cellar) -name libxml2.dylib)
libserialport_loc=$(find $(brew --cellar) -name libserialport.dylib)
libiio_loc=$(find . -name iio | grep Versions)
libiioheader_loc=$(find . -name iio.h)

if [ ! -f "${libusb_loc}" ]; then
        echo "libusb library not found"
        exit 1
fi
if [ ! -f "${libxml_loc}" ]; then
        echo "libxml library not found"
        exit 1
fi
if [ ! -f "${libserialport_loc}" ]; then
        echo "libserialport library not found"
        exit 1
fi

# Create links to framework files
mkdir -p usr/local/{lib,include}
ln -fs "${libiio_loc}" usr/local/lib/libiio.dylib
ln -fs "${libiioheader_loc}" usr/local/include/iio.h

# Copy dependent libs to local libs
cp "${libusb_loc}" usr/local/lib/
cp "${libxml_loc}" usr/local/lib/
cp "${libserialport_loc}" usr/local/lib/
chmod +w usr/local/lib/libusb-1.0.dylib
chmod +w usr/local/lib/libxml2.dylib
chmod +w usr/local/lib/libserialport.dylib
install_name_tool -id @rpath/libusb-1.0.dylib usr/local/lib/libusb-1.0.dylib
install_name_tool -id @rpath/libxml2.dylib usr/local/lib/libxml2.dylib
install_name_tool -id @rpath/libserialport.dylib usr/local/lib/libserialport.dylib

# Update rpath of library
install_name_tool -change "${libusb_loc}" "@rpath/libusb-1.0.dylib" "${libiio_loc}"
install_name_tool -change "${libxml_loc}" "@rpath/libxml2.dylib" "${libiio_loc}"
install_name_tool -change "${libserialport_loc}" "@rpath/libserialport.dylib" "${libiio_loc}"
install_name_tool -add_rpath @loader_path/. "${libiio_loc}"

# Update tools
cd Library/Frameworks/iio.framework/Tools
for tool in *;
do
        install_name_tool -add_rpath @loader_path/../.. "${tool}"
done
cd ../../../../

# Remove old tar and create new one
rm "../${tarname}"
tar -czf "../${tarname}" .
cd ..
rm -rf temp
