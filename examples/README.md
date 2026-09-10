# libiio Examples

The libiio is a cross platform library for interfacing with Linux IIO devices. 

These are some primitive examples of using the libiio library. 

These are only build if the -DWITH_EXAMPLES=ON is provided to the standard Cmake.

While the libiio library is tested on many operating systems, these examples are only tested on Linux.

More examples using all languages supported by libiio are encouraged to be added here.
When adding a new example, please update this list. 

## ad9361-iiostream
  * Language : C

This example libiio program is meant to exercise the features of IIO functionality on the AD9361 found on the AD-FMCOMMS2-EBZ, AD-FMCOMMS3-EBZ, and the ADRV9361-Z7035 RF SOM.
It takes the uri as the only argument. for example : `./ad9361-iiostream usb:3.32.5`

## ad9361-iioblock
  * Language : C

This example libiio program targets the same hardware as `ad9361-iiostream` and does the
same trivial work with the samples, but streams with the low level block API
(`iio_buffer_open()` / `iio_buffer_stream_create_block()` / `iio_block_enqueue()` /
`iio_block_dequeue()`) instead of the higher level `iio_stream` helper. The two are meant
to be read side by side: what differs is that the blocks are created, handed to the
hardware and taken back explicitly.

A block belongs to exactly one side at a time. `iio_block_enqueue()` gives it to the
hardware and `iio_block_dequeue()` waits until the hardware is done with it. Four blocks of
1 MiB are kept in flight per direction, so the DMA always has somewhere to write, or
something ready to send, while the program is still busy with the previous block.

Received samples get their I and Q swapped in place and transmitted samples are zeroed.
The two directions also show the two ways of walking a block: `iio_block_first()` with
`iio_block_end()` on RX, and `iio_block_foreach_sample()` on TX, which visits every sample
of every channel in the mask and so needs no assumption about sample width or channel
layout.

It takes an optional uri as its last argument. For example : `./ad9361-iioblock ip:192.168.2.1`
Run `./ad9361-iioblock -h` for the full usage.

## ad9371-iiostream
  * Language : C

This example libiio program is meant to exercise the features of IIO functionality on the AD9371.
It takes the uri as the only argument. for example : `./ad9371-iiostream ip:192.168.2.1`

## adrv9002-iiostream
  * Language : C

This example libiio program is meant to exercise the features of IIO functionality on the ADRV9002.
It takes the uri as the only argument. for example : `./adrv9002-iiostream ip:192.168.2.1`


## adrv9009-iiostream
  * Language : C

This example libiio program is meant to exercise the features of IIO functionality on the ADRV9009.
It takes the uri as the only argument. for example : `./adrv9009-iiostream ip:192.168.2.1`

## dummy-iiostream
  * Language : C

This example libiio program is meant to exercise the features of IIO present in the sample dummy IIO device in the linux kernel.
For buffered access it relies on the hrtimer trigger but could be modified to use the sysfs trigger. 
No hardware should be required to run this program.

## iio-monitor
  * Language : C
  * Requirements : Curses Development Kit (libcdk5-dev); pthreads; ncurses; libiio

A Curses based application which implements real time monitoring of IIO non-buffer samples.

## iopp-enum
  * Language : C++

Demonstrates the usage of the C++ API.
