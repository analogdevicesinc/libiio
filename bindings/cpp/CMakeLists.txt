cmake_minimum_required(VERSION 3.10)

project(iiopp-enum CXX)
add_executable(iiopp-enum examples/iiopp-enum.cpp iiopp.h ${LIBIIO_RC})
target_include_directories(iiopp-enum PRIVATE ./)
set_target_properties(iiopp-enum PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(iiopp-enum iio)
