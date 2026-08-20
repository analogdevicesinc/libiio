.. _snippet-iiod-usb:

iiod USB Snippet (iiod-usb)
###########################

.. code-block:: console

   west build -S iiod-usb [...]

Overview
********

This snippet enables the iiod server over a native USB interface. The device
appears to the host as a vendor-specific USB interface which libiio reaches
through the ``usb:`` URI, so no host-side configuration is required beyond
permission to access the device.

The snippet requires a board with a USB device controller, and the USB device
which should be used is configured using :ref:`devicetree`.

Requirements
************

Hardware support for:

- :kconfig:option:`CONFIG_USB_DEVICE_STACK_NEXT`

A devicetree node with node label ``zephyr_udc0`` that points to an enabled USB
device node with driver support. This should look roughly like this in
:ref:`your devicetree <get-devicetree-outputs>`:

.. code-block:: DTS

   zephyr_udc0: usbd@deadbeef {
   	compatible = "vnd,usb-device";
        /* ... */
   };

The node must have a controller driver behind it. A board that declares
``zephyr_udc0`` as a placeholder without driver support does not qualify: the
USB device is linked against the controller device, so the build fails to
resolve it.

The USB device controller must provide at least two bulk endpoints per pipe,
one IN and one OUT. The default of three pipes therefore requires six bulk
endpoints; reduce ``num-pipes`` on controllers with fewer.

Simulated USB on native_sim
***************************

:zephyr:board:`native_sim <native_sim>` declares ``zephyr_udc0`` as a
placeholder with no controller driver, so it does not meet the requirement
above. On that board the snippet replaces the placeholder with a virtual host
and device controller pair and exports the device to the Linux kernel over
USB/IP, which makes the transport usable without USB hardware.

Attach the exported device before connecting to it:

.. code-block:: console

   west build -p -b native_sim samples/iiod/ -S iiod-usb
   ./build/zephyr/zephyr.exe &

   sudo modprobe vhci-hcd
   sudo usbip attach -r 127.0.0.1 -b 1-1
   iio_info -u usb:

Binding the device to ``vhci-hcd`` requires root. Detach it with
``sudo usbip detach -p 00`` when finished.
