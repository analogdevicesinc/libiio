C++ Bindings
============

The C++ bindings provide a modern C++ interface to libiio with RAII resource
management, exceptions for error handling, and idiomatic C++ patterns.

Requirements
------------

- C++17 or higher (recommended)
- C++11 with Boost (for ``boost::optional``)

The C++ bindings are header-only. Include ``<iio.hpp>`` to use them.

Source Code
-----------

The C++ bindings source code is available at:
:git+libiio:`bindings/cpp <gui+:bindings/cpp>`

API Classes
-----------

.. toctree::
   :maxdepth: 2

   context
   device
   channel
   buffer
   block
   stream
   bufferstream
   channelsmask
   scan
   eventstream
   event
   attr
   ptr
   cstr
   error

Examples
--------

.. toctree::
   :maxdepth: 2

   examples
