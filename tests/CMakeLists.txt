cmake_minimum_required(VERSION 2.8.8)

project(iio_genxml  C)
project(iio_info    C)
project(iio_readdev C)

add_executable(iio_genxml  iio_genxml.c)
add_executable(iio_info    iio_info.c)
add_executable(iio_readdev iio_readdev.c)

target_link_libraries(iio_genxml  iio)
target_link_libraries(iio_info    iio)
target_link_libraries(iio_readdev iio)

if(NOT SKIP_INSTALL_ALL)
	install(TARGETS iio_genxml  RUNTIME DESTINATION bin)
	install(TARGETS iio_info    RUNTIME DESTINATION bin)
	install(TARGETS iio_readdev RUNTIME DESTINATION bin)
endif()
