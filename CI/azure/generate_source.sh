#!/bin/bash -e
 
DESCRIPTOR_FILE="libiio_${VERSION}.dst"
do_hash() {
    HASH_NAME=$1
    HASH_CMD=$2
    echo "${HASH_NAME}:"
    for f in $(find -type f -name *.tar*); do
        f=$(echo $f | cut -c3-)
        if [ "$f" = "libiio_${VERSION}.dsc" ]; then
            continue
        fi
        echo " $(${HASH_CMD} ${f}  | cut -d" " -f1) $(wc -c $f)"
    done
}
 
cat << EOF > ${DESCRIPTOR_FILE}
Format: 3.0 (quilt)
Source: libiio
Binary: libiio
Version: ${VERSION}
Maintainer: Engineerzone <https://ez.analog.com/community/linux-device-drivers>
Standards-Version: 4.6.2
EOF
do_hash "Checksums-Sha1" "sha1sum"
do_hash "Checksums-Sha256" "sha256sum"
do_hash "Files" "md5sum"