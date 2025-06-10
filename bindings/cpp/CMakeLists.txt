cmake_minimum_required(VERSION 3.13)

project(iiopp-enum LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
    #include <optional>
    int main() {
        std::optional<int> o;
        return 0;
    }
" HAVE_STD_OPTIONAL)

if(NOT HAVE_STD_OPTIONAL)
	find_package(Boost REQUIRED)
endif()

add_executable(iiopp-enum examples/iiopp-enum.cpp iiopp.h ${LIBIIO_RC})
target_include_directories(iiopp-enum PRIVATE ./)
set_target_properties(iiopp-enum PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(iiopp-enum iio)
