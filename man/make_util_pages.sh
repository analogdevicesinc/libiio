#!/bin/sh

dir=$1
if [ -z "${dir}" -o ! -f "${dir}/iio_common_cmds.var" -o ! -f "${dir}/iio_common_opts.var" -o ! -f "${dir}/iio_common_footer.var" ] ; then
	echo "not pointing to src directory ${dir}" 1>&2
	exit
fi
file=$2
if [ -z "${file}" -o ! -f "${file}" ] ; then
	echo "missing 2nd argument file to manipulate ${file}" 1>&2
	exit
fi

app=$(grep "^.TH" "${file}" | head -1 | awk '{print $2}')

sed '/##COMMON_COMMANDS_START##/Q' "${file}"
sed "s/##APP_NAME##/${app}/g" "${dir}/iio_common_cmds.var"
sed -n '/^##COMMON_COMMANDS_STOP##/,${p;/^##COMMON_OPTION_START##/q}' "${file}" |  grep -v "##COMMON_"
sed "s/##APP_NAME##/${app}/g" "${dir}/iio_common_opts.var"
sed -ne '/^##COMMON_OPTION_STOP#/,$ p' "${file}" | grep -v "##COMMON_OPTION_STOP##"
sed "s/##APP_NAME##/${app}/g" "${dir}/iio_common_footer.var"
