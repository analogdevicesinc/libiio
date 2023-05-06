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

if [ "${error}" -eq "1" ] ; then
	exit 1
fi
