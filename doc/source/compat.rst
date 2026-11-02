0.x Compatibility API
=====================

libiio provides an optional compatibility layer, ``iio-compat``, which
implements the old libiio 0.x API on top of the current library. It exists
to help existing applications keep building while they migrate to the
version 1 API described in :doc:`api`; see :doc:`migration` for a guide on
porting away from it.

This compatibility layer is a separate library (``libiio-compat``, enabled
with the ``LIBIIO_COMPAT`` build option) and is documented here on its own,
rather than alongside the version 1 API.

.. doxygengroup:: CompatAPI
    :content-only:
    :members:
