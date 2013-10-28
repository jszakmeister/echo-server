***********
echo-server
***********

This is a rudimentary implementation of an echo server.  It's primary purpose is
an educational tool in teaching some of our junior engineers some of the basics
about Linux/Unix programming.  It's not meant to be a production-ready
implementation, but it is meant to be fairly rigorous.

Building echo-server
====================

A simple ``make`` will build a simplistic version of ``echo-server``.  There are
some defines you can enable to get a version with more features:

* ``ENABLE_ALARM`` - This adds a basic timer to interrupt system calls.  It also
  prints out the number of connections received over time--but only as a small
  attempt to demonstrate why someone might do such a thing.  The primary purpose
  is to interrupt system calls.

* ``ENABLE_FORKING`` - Enables forking to handle client connections.

* ``ENABLE_THREADING`` - Use threads for client connections instead.

* ``ENABLE_DAEMON`` - Make the echo server daemonize itself and separate from
  the controlling terminal.

Note, you'll want to do a ``make clean`` before changing any of the options so
that the executable will be properly rebuilt.  To enable a feature, just do the
following:

.. code-block:: text

    make clean
    make -DENABLE_FEATURE=1

For example, to enable threading do:

.. code-block:: text

    make clean
    make -DENABLE_THREADING=1


Running
=======

Currently, there are no options you can pass on the command line.  Simply run
the resultant ``echo-server`` binary.  You can then telnet or netcat to port
8888 to reach the echo server.
