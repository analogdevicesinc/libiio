#!/bin/bash
set -e

KERNEL_TYPES="/tmp/mainline_types.h"
KERNEL_MODIFIER="/tmp/modifier.c"
IIOH="./include/iio/iio.h"
CHANNELC="./channel.c"
CHANNELC_SHARP="./bindings/csharp/Channel.cs"
IIO_PY="./bindings/python/iio.py"

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

if [ ! -f ${IIO_PY} ] ; then
	echo can not find ${IIO_PY}
	exit 1
fi

rm -f ${KERNEL_TYPES} ${KERNEL_MODIFIER}
wget -O ${KERNEL_TYPES} https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/include/uapi/linux/iio/types.h
wget -O ${KERNEL_MODIFIER} https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/drivers/iio/industrialio-core.c

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

sed '/\[IIO_MOD_.*\] =/!d' ${KERNEL_MODIFIER} | sed 's/,$//' > /tmp/kernel_modifier
sed '/\[IIO_[A-Z]*\] =/!d' ${KERNEL_MODIFIER} | sed 's/,$//' >> /tmp/kernel_modifier
while IFS="" read -r p ; do
	key=$(echo $"${p}" | sed -e 's/^[ \t]*//' -e 's/\[/\\\[/g' -e 's/\]/\\\]/g' )
	count=$(grep "${key}" ./channel.c | wc -l)
	if [ "$count" -eq "0" ] ; then
		wrong=$(grep $(echo "${p}" | sed -e 's/^.*\[//' -e 's/\].*$//') ./channel.c | \
				sed -e 's/^[[:space:]]*//' -e 's/,.*$//')
		echo "${p} set to (${wrong}) in channel.c"
		ret=1
	fi
done < /tmp/kernel_modifier

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

echo Checking Python bindings

python_enums=("ChannelType" "ChannelModifier" "EventType" "EventDirection")

for i in {0..3}
do
	echo "looking for ${python_enums[i]}"
	sed "0,/^class ${python_enums[i]}/d" ${IIO_PY} | \
		sed '0,/^$/d' | sed -n '/^$/q;p'| sed -e 's/^[ \t]*//' -e 's/ .*//' | \
		grep -v IIO_CHAN_TYPE_UNKNOWN > "/tmp/libiio_py_${python_enums[i]}"

	echo "Differences in ${python_enums[i]}"
	set +e
	diff -u -w "/tmp/libiio_py_${python_enums[i]}" "/tmp/kernel_${iio_groups[i]}"
	count=$(diff -u -w  "/tmp/libiio_py_${python_enums[i]}" "/tmp/kernel_${iio_groups[i]}" | wc -l)
	set -e
	if [ "$count" -ne "0" ] ; then
		ret=1
		echo "difference between upstream kernel types.h and iio.py in ${python_enums[i]}"
	else
		echo none
	fi
done

rm -f /tmp/kernel_modifier /tmp/libiio_iio_modifier /tmp/libiio_iio_chan_type ${KERNEL_TYPES} ${KERNEL_MODIFIER}
exit $ret
