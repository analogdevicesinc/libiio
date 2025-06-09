cmake_minimum_required(VERSION 3.13)
project(iiopp-enum LANGUAGES CXX)

# Prefer, but do not require, C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

include(CheckCXXSourceCompiles)

# Check for std::optional
check_cxx_source_compiles("
    #include <optional>
    int main() {
        std::optional<int> o;
        return 0;
    }
" HAS_STD_OPTIONAL)

if(NOT HAS_STD_OPTIONAL)
    find_package(Boost COMPONENTS optional REQUIRED)
    if(Boost_FOUND)
        set(CMAKE_REQUIRED_INCLUDES ${Boost_INCLUDE_DIRS})
        check_cxx_source_compiles("
            #include <boost/optional.hpp>
            int main() {
                boost::optional<int> o;
                return 0;
            }
        " HAS_BOOST_OPTIONAL)
        unset(CMAKE_REQUIRED_INCLUDES)
        if(HAS_BOOST_OPTIONAL)
            set(USE_BOOST_OPTIONAL TRUE)
        else()
            message(FATAL_ERROR "Neither std::optional nor boost::optional is available")
        endif()
    else()
        message(FATAL_ERROR "Boost not found and std::optional is unavailable")
    endif()
else()
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_executable(iiopp-enum examples/iiopp-enum.cpp ${LIBIIO_RC})
target_include_directories(iiopp-enum PRIVATE ./)
target_link_libraries(iiopp-enum PRIVATE iio)

if(USE_BOOST_OPTIONAL)
    target_include_directories(iiopp-enum PRIVATE ${Boost_INCLUDE_DIRS})
endif()
