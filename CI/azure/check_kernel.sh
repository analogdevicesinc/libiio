#!/bin/bash
set -e

KERNEL_TYPES="/tmp/mainline_types.h"
IIOH="./iio.h"
CHANNELC="./channel.c"
CHANNELC_SHARP="./bindings/csharp/Channel.cs"

if [ ! -f ${IIOH} ] ; then
	echo can not find ${IIOH}
	exit 1
fi

if [ ! -f ${CHANNELC} ] ; then
	echo can not find ${CHANNELC}
	exit 1
fi

if [ ! -f ${CHANNELC_SHARP} ] ; then
	echo can not find ${CHANNELC_SHARP}
	exit 1
fi

rm -f ${KERNEL_TYPES}
wget -O ${KERNEL_TYPES} https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/include/uapi/linux/iio/types.h

iio_groups=("iio_chan_type" "iio_modifier" "iio_event_type" "iio_event_direction")
ret=0

for enum in "${iio_groups[@]}";
do
	echo looking for ${enum}
	rm -f /tmp/kernel_${enum} /tmp/libiio_${enum}
	sed "0,/${enum}/d" ${KERNEL_TYPES}  | sed -n '/}/q;p' > /tmp/kernel_${enum}
	sed "0,/^enum.*${enum}/d" ${IIOH} | sed -n '/}/q;p' | grep -v IIO_CHAN_TYPE_UNKNOWN > /tmp/libiio_${enum}
	echo Differences in ${enum}
	# diff exit status of 1 means a difference, not an error
	set +e
	diff -u  /tmp/libiio_${enum} /tmp/kernel_${enum}
	count=$(diff -u  /tmp/libiio_${enum} /tmp/kernel_${enum} | wc -l)
	set -e
	if [ "$count" -ne "0" ] ; then
		ret=1
		echo difference between upstream kernel types.h and iio.h in ${enum}
	else
		echo none
	fi
done

for enum in iio_chan_type_name_spec modifier_names
do
	sed "0,/^static.*${enum}/d" ${CHANNELC} | sed -n '/}/q;p' | \
		grep -v IIO_CHAN_TYPE_UNKNOWN > /tmp/libiio_${enum}
done

while IFS="" read -r p ; do
	key=$(echo "${p//[[:space:],]/}")
	count=$(grep "\[$key\]" /tmp/libiio_iio_chan_type_name_spec | wc -l)
	if [ "$count" -eq "0" ] ; then
		echo "$key missing from channel.c iio_chan_type_name_spec"
		ret=1
	fi
done < /tmp/libiio_iio_chan_type

echo
sed -i '/IIO_NO_MOD/d' /tmp/libiio_iio_modifier

while IFS="" read -r p ; do
	key=$(echo "${p//[[:space:],]/}")
	count=$(grep "\[$key\]" /tmp/libiio_modifier_names | wc -l)
	if [ "$count" -eq "0" ] ; then
		echo "$key missing from channel.c modifier_names"
		ret=1
	fi
done < /tmp/libiio_iio_modifier

# Cleanup up leading tabs & spaces and trailing commas
for group in "${iio_groups[@]}";
do
    sed -i -e 's/^[ \t]*//' -e 's/,$//' "/tmp/kernel_${group}"
done

echo Checking C# bindings

csharp_enums=("ChannelType" "ChannelModifier")
for i in {0..1}
do
	echo "looking for ${csharp_enums[i]}"
	sed "0,/^[[:space:]]*public enum ${csharp_enums[i]}/d" ${CHANNELC_SHARP} | \
		sed -n '/}/q;p' | sed '1{/{/d}' | sed -e 's/^[ \t]*//' -e 's/,$//' | \
		grep -v IIO_CHAN_TYPE_UNKNOWN > "/tmp/libiio_csharp_${csharp_enums[i]}"

	echo "Differences in ${csharp_enums[i]}"
	set +e
	diff -u -w "/tmp/libiio_csharp_${csharp_enums[i]}" "/tmp/kernel_${iio_groups[i]}"
	count=$(diff -u -w  "/tmp/libiio_csharp_${csharp_enums[i]}" "/tmp/kernel_${iio_groups[i]}" | wc -l)
	set -e
	if [ "$count" -ne "0" ] ; then
		ret=1
		echo "difference between upstream kernel types.h and Channels.cs in ${csharp_enums[i]}"
	else
		echo none
	fi
done

exit $ret
