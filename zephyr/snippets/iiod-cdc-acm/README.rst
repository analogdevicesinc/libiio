.. _snippet-iiod-cdc-acm:

iiod CDC-ACM Snippet (iiod-cdc-acm)
###################################

.. code-block:: console

   west build -S iiod-cdc-acm [...]

Overview
********

This snippet enables the iiod server over a CDC ACM UART. The USB device which
should be used is configured using :ref:`devicetree`.

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
