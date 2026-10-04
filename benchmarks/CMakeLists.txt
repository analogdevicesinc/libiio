cmake_minimum_required(VERSION 3.13)

set(IIO_BENCHMARK_TARGETS
    bench_context
    bench_attr
    bench_block
    bench_buffer
    bench_streaming)

add_library(iio_bench_common OBJECT bench_common.c)
target_link_libraries(iio_bench_common LINK_PRIVATE iio)
set_target_properties(
    iio_bench_common
    PROPERTIES C_STANDARD 99
               C_STANDARD_REQUIRED ON
               C_EXTENSIONS OFF)

foreach (bench ${IIO_BENCHMARK_TARGETS})
    add_executable(${bench} ${bench}.c)
    set_target_properties(
        ${bench}
        PROPERTIES C_STANDARD 99
                   C_STANDARD_REQUIRED ON
                   C_EXTENSIONS OFF)
    target_link_libraries(${bench} LINK_PRIVATE iio iio_bench_common m)
endforeach ()
