#!/bin/sh

# Check the project CMake for options, which are not described in the README_BUILD.md file
# At the same time, check to make sure the defaults are described properly.

error=0

options() {
	for file in $(find ./ -not \( -path ./deps -prune \) -name CMakeLists.txt)
	do
		grep option[[:space:]]*\( "${file}" | \
			sed -e "s/^[[:space:]]*//g" -e "s/(/ /g" | \
			awk '{print $2}'
	done | sort | uniq 
}

for opt in $(options)
do
	default=$(for file in $(find ./ -not \( -path ./deps -prune \) -name CMakeLists.txt)
	do
		grep "option[[:space:]]*(${opt} " "${file}" | \
			sed -e "s/^[[:space:]]*//g" -e "s/)[[:space:]]*$//" | \
			awk '{print $NF}'
	done)
	if ! grep -q "${opt}.*${default}" README_BUILD.md ; then
		echo "no match with ${opt} set with ${default}"
		grep -R "${opt}" ./*
		error=1
	fi
done

# check if any cmake options don't print out at the end of the cmake process
for f in $(find ./ -not \( -path ./deps -prune \) -name CMakeLists.txt)
do
	for i in $(grep -i "option[[:space:]]*(" "${f}" | sed -e "s/^[[:space:]]*//g" -e "s/(/ /g" | awk '{print $2}' | sort | uniq)
	do
		a=$(grep -i "toggle_iio_feature.*${i}" $(find ./ -not \( -path ./deps -prune \) -name CMakeLists.txt))
		if [ -z "${a}" ] ; then
			echo "${f} defines \"${i}\" as option, but it is missing toggle_iio_feature"
			error=1
		fi
	done
done

if [ "${error}" -eq "1" ] ; then
	exit 1
fi
