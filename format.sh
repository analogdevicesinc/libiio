#!/bin/bash

############################################################################
# This script will format all C source files (.h and .c) that are not
# mentioned in .clangformatignore, and all CMake files (*.cmake and CMakeLists.txt)
# that are not mentioned in .cmakeformatignore.
# Note that the script has to be run from the same folder where
# the ignore files (.clangformatignore and .cmakeformatignore) are located.
###########################################################################

############################################################################
# Check if the file given as input has .h or .c extension
############################################################################

# Check if the file given as input has .h or .c extension
is_source_file() {
    [[ "$1" == *.h || "$1" == *.c ]]
}

# Check if the file is a CMake file (.cmake or CMakeLists.txt)
is_cmake_file() {
    case "$1" in
        *.cmake) return 0 ;;
        */CMakeLists.txt|*\\CMakeLists.txt|CMakeLists.txt) return 0 ;;
        *) return 1 ;;
    esac
}

# Check if the file passed as argument is ignored or not by a given ignore file
is_not_ignored_in() {
    local file="$1"
    local ignore_file="$2"
    if [[ ! -f "$ignore_file" ]]; then
        return 0
    fi
    while IFS= read -r entry || [[ -n "$entry" ]]; do
        # Skip empty lines and comments
        [[ -z "$entry" || "$entry" =~ ^# ]] && continue
        # If entry is a directory and file is inside it
        if [[ -d "$entry" && "$file" == "$entry"* ]]; then
            return 1
        fi
        # If entry matches the file exactly
        if [[ "$file" == "$entry" ]]; then
            return 1
        fi
    done < "$ignore_file"
    return 0
}



format_all() {
    git ls-tree -r --name-only HEAD | while read -r file; do
        if is_source_file "$file" && is_not_ignored_in "$file" .clangformatignore; then
            clang-format -i "$file"
        fi
        if is_cmake_file "$file" && is_not_ignored_in "$file" .cmakeformatignore; then
            cmake-format -i "$file"
        fi
    done
}

format_all
