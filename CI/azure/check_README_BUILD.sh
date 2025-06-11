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
		grep -E "(^|\s)(option|iio_option)[[:space:]]*\([[:space:]]*${opt}[[:space:]]+" "${file}" | \
        sed -e "s/^[[:space:]]*//g" -e "s/)[[:space:]]*$//" | \
        awk '{
            for(i=1;i<=NF;i++) {
                if ($i == "ON" || $i == "OFF") {
                    print $i
                    break
                }
            }
        }'
	done)
	if ! grep -q "${opt}.*${default}" README_BUILD.md ; then
		echo "no match with ${opt} set with ${default}"
		# grep -R "${opt}" ./*
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
            # Check if the option is appended to OPTIONS_LISTS
            awk -v opt="$i" '
                /list[[:space:]]*\([[:space:]]*APPEND[[:space:]]+OPTIONS_LISTS/ {inlist=1}
                inlist {
                    if ($0 ~ /\)/) inlist=0
                    if (index($0, opt)) found=1
                }
                END {exit !found}
            ' $(find ./ -not \( -path ./deps -prune \) -name CMakeLists.txt)
            if [ $? -ne 0 ]; then
                echo "${f} defines \"${i}\" as option, but it is missing toggle_iio_feature and is not appended to OPTIONS_LISTS"
                error=1
            fi
        fi
    done
done

if [ "${error}" -eq "1" ] ; then
	exit 1
fi
