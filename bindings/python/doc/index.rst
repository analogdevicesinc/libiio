libiio Python Bindings
==================================

Python bindings for the Industrial I/O interface library. 

Installation
==================

The libiio python bindings can be installed from pip

.. code-block:: bash

  (sudo) pip install libiio

or by grabbing the source directly

.. code-block:: bash

  git clone https://github.com/analogdevicesinc/libiio.git
  cd bindings/python
  (sudo) python3 setup.py install

.. note::

  On Linux the libiio python bindings are sometimes installed in locations not on path. On Ubuntu this is a common fix

  .. code-block:: bash

    export PYTHONPATH=$PYTHONPATH:/usr/lib/python{python-version}/site-packages

.. toctree::
   :maxdepth: 1
   :caption: Contents:

Components
==================
.. toctree::
   :maxdepth: 1

   context
   buffer
   device
   channel
   trigger
   examples
   tools

Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`
