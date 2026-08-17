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
