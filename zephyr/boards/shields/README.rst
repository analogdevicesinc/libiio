.. _iiod_boards_shields:

Overview
********

The shields in this repo correspond one-to-one to shields found in the
upstream Zephyr repository, and expose the devices from those shields as IIO
devices and channels.

To use the shields, you should build your application with both the "base"
shield and the IIO shield included, e.g.:

.. zephyr-app-commands::
   :zephyr-app: samples/iiod
   :board: adafruit_metro_rp2040/rp2040
   :snippets: iiod-usb
   :shield: adafruit_aht20;iio_adafruit_aht20
   :goals: build flash
   :compact:
