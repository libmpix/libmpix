@page getting_started Getting Started
@brief Building and running libmpix in various contexts

## Using libmpix in a project

**libmpix** is meant as a portable library, and various platforms are supported out of the box,
with an example repo for each:

- [Zephyr example repo](https://github.com/libmpix/libmpix_example_zephyr)

- [POSIX (Linux/BSD/MacOS) example repo](https://github.com/libmpix/libmpix_example_posix)

If your platform is not supported, you would need to provide a "port" that provides the glue logic
and build rules to integrate it as a library as designed by the target system.
See the subdirectories of [`ports`][1] for examples of how to port libmpix to new systems.

[1]: https://github.com/libmpix/libmpix/tree/zephyr/ports
