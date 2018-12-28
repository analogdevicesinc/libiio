#!/bin/bash
# Based on a test script from avsm/ocaml repo https://github.com/avsm/ocaml
set -ex

if [ $# -ne 1 ] ; then
	echo Must include debian distribution ie wheezy, jessie, stretch, or buster
	exit -1
fi
if [[ ! $1 =~ ^wheezy|jessie|stretch|buster$ ]] ; then
	echo Must include debian distribution ie wheezy, jessie, stretch, or buster
	exit -1
fi

CHROOT_DIR=/tmp/arm-chroot
MIRROR=http://archive.raspbian.org/raspbian
# wheezy = 7 (2013-05-04); jessie = 8 (2015-04-26); stretch = 9 (2017-06-17); buster = 10 (2019-03-12)
VERSION=$1
echo building for ${VERSION}
CHROOT_ARCH=armhf

# Host dependencies
sudo apt-get update
sudo apt-get install -qq -y qemu-user-static binfmt-support sbuild wget debian-archive-keyring ubuntu-keyring gnupg libudev1 libudev-dev liblzma-dev libglib2.0-dev libfdt-dev libpixman-1-dev zlib1g-dev
# need a newer version of qemu :(
QEMU_DIR=/tmp/qemu
sudo mkdir -p ${QEMU_DIR}
sudo chown ${USER} /tmp/qemu
cd ${QEMU_DIR}
wget https://download.qemu.org/qemu-3.1.0.tar.xz
tar xf qemu-3.1.0.tar.xz
cd qemu-3.1.0
mkdir build
mkdir install
cd build
../configure --prefix=${QEMU_DIR}/install --disable-bsd-user --disable-guest-agent --disable-strip --disable-werror --disable-gcrypt --disable-debug-info --disable-debug-tcg --disable-docs --disable-tcg-interpreter --enable-attr --disable-brlapi --disable-linux-aio --disable-bzip2 --disable-bluez --disable-cap-ng --disable-curl --disable-fdt --disable-glusterfs --disable-gnutls --disable-nettle --disable-gtk --disable-rdma --disable-libiscsi --disable-vnc-jpeg --disable-kvm --disable-lzo --disable-curses --disable-libnfs --disable-numa --disable-opengl --disable-vnc-png --disable-rbd --disable-vnc-sasl --disable-sdl --disable-seccomp --disable-smartcard --disable-snappy --disable-spice --disable-libssh2 --disable-libusb --disable-usb-redir --disable-vde --disable-vhost-net --disable-virglrenderer --disable-virtfs --disable-vnc --disable-vte --disable-xen --disable-xen-pci-passthrough --disable-xfsctl --enable-linux-user --disable-system --disable-blobs --disable-tools --target-list=arm-linux-user --static --disable-pie
make -j $(nproc)
make install
ls -lR ${QEMU_DIR}/install
cd /tmp

# per https://wiki.ubuntu.com/DebootstrapChroot
if [[ "$VERSION" =~ ^wheezy|jessie$ ]] ; then
	sudo apt-get install -qq -y debootstrap

elif [[ "$VERSION" =~ ^stretch$ ]] ; then
	sudo add-apt-repository -r "deb http://archive.ubuntu.com/ubuntu $(lsb_release -cs)-updates main restricted universe multiverse "
	sudo apt-get install -qq -y -t $(lsb_release -cs)-updates debootstrap
elif [[ "$VERSION" =~ ^buster$ ]] ; then
	wget http://http.us.debian.org/debian/pool/main/d/debootstrap/debootstrap_1.0.111_all.deb -O /tmp/debootstrap_1.0.111_all.deb
	sudo dpkg --install /tmp/debootstrap_1.0.111_all.deb
fi

# Create chrooted environment
sudo mkdir ${CHROOT_DIR}
sudo debootstrap --foreign --no-check-gpg --include=fakeroot,build-essential --arch=${CHROOT_ARCH} ${VERSION} ${CHROOT_DIR} ${MIRROR}
sudo cp ${QEMU_DIR}/install/bin/qemu-arm ${CHROOT_DIR}/usr/bin/qemu-arm-static
${CHROOT_DIR}/usr/bin/qemu-arm-static -version
sudo chroot ${CHROOT_DIR} ./debootstrap/debootstrap --second-stage
sudo sbuild-createchroot --arch=${CHROOT_ARCH} --foreign --setup-only ${VERSION} ${CHROOT_DIR} ${MIRROR}

# Create file with environment variables which will be used inside chrooted
# environment
echo "export ARCH=${ARCH}" > envvars.sh
echo "export TRAVIS_BUILD_DIR=${TRAVIS_BUILD_DIR}" >> envvars.sh
chmod a+x envvars.sh

# Install dependencies inside chroot
sudo chroot ${CHROOT_DIR} dpkg --add-architecture ${CHROOT_ARCH}
sudo chroot ${CHROOT_DIR} dpkg --remove-architecture amd64
sudo chroot ${CHROOT_DIR} apt-get update
sudo chroot ${CHROOT_DIR} apt-get --allow-unauthenticated install -qq -y locales
sudo chroot ${CHROOT_DIR} locale 
sudo chroot ${CHROOT_DIR} bash -c "echo en_US.UTF-8 UTF-8 > /etc/locale.gen"
sudo chroot ${CHROOT_DIR} locale-gen
#sudo chroot ${CHROOT_DIR} bash -c "echo -e 'LANG=\"en_US.UTF-8\"\\nLANGUAGE=\"en_US:en\"\\n' > /etc/default/locale"
sudo chroot ${CHROOT_DIR} apt-get --allow-unauthenticated install -qq -y build-essential git m4 sudo python cmake

# Create build dir and copy travis build files to our chroot environment
sudo mkdir -p ${CHROOT_DIR}/${TRAVIS_BUILD_DIR}
sudo rsync -av ${TRAVIS_BUILD_DIR}/ ${CHROOT_DIR}/${TRAVIS_BUILD_DIR}/

# Indicate chroot environment has been set up
sudo touch ${CHROOT_DIR}/.chroot_is_done

# Call standard before_install_linux in chroot environment
sudo chroot ${CHROOT_DIR} bash -c "cd ${TRAVIS_BUILD_DIR} && pwd && ./CI/travis/before_install_linux"

