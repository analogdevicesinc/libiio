#!/bin/bash
set -e

KERNEL_TYPES="/tmp/mainline_types.h"
KERNEL_MODIFIER="/tmp/modifier.c"
IIOH="./include/iio/iio.h"
CHANNELC="./channel.c"

if [ ! -f ${IIOH} ] ; then
	echo can not find ${IIOH}
	exit 1
fi

if [ ! -f ${CHANNELC} ] ; then
	echo can not find ${CHANNELC}
	exit 1
fi

rm -f ${KERNEL_TYPES} ${KERNEL_MODIFIER}
wget -O ${KERNEL_TYPES} https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/include/uapi/linux/iio/types.h
wget -O ${KERNEL_MODIFIER} https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/drivers/iio/industrialio-core.c

ret=0

for enum in iio_chan_type iio_modifier iio_event_type iio_event_direction
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


rm -f /tmp/kernel_modifier /tmp/libiio_iio_modifier /tmp/libiio_iio_chan_type ${KERNEL_TYPES} ${KERNEL_MODIFIER}
exit $ret
