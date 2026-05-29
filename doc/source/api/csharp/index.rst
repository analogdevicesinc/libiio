C# Bindings
===========

The C# bindings provide a .NET interface to libiio, allowing applications written in C#
to access Industrial I/O devices.

Installation
------------

The C# bindings are distributed as ``libiio-sharp.dll``.

1. Build libiio with C# bindings enabled::

      cmake -DCSHARP_BINDINGS=ON ..
      make
      make install

2. Copy ``libiio-sharp.dll`` to your project
3. Add reference to the DLL in your C# project

Source Code
-----------

The C# bindings source code is available at:
:git+libiio:`bindings/csharp <gui+:bindings/csharp>`

API Classes
-----------

.. toctree::
   :maxdepth: 2

   context
   device
   channel
   buffer
   block
   trigger
   scan
   eventstream
   event
   stream
   iioobject
   attr
   channelsmask
   bufferstream
   version
   iiocontextparams
   iiologlevel

Examples
--------

.. toctree::
   :maxdepth: 2

   examples
