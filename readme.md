# IPPUSBXD [![Coverity analysis status](https://scan.coverity.com/projects/2634/badge.svg)](https://scan.coverity.com/projects/2634)

Version 1.21

About
=======
IPPUSBXD is a userland driver for ipp over usb class usb devices. It has been
designed for Linux but uses a cross platform usb library allowing eventual
porting to Windows and other non-posixs platforms.

The ipp over usb standard was ratified by the usb forum in 2012. As of 2014 Mac
OSX implemented this standard and with the addition of ippusbxd soon linux shall
as well.

IPPUSBXD depends on posixs threads, posixs networking, and libusb as developed
by the community at libusb.info

IPPUSBXD has the following advantages;

1. At runtime links only with libc, pthreads, and libusb. On a typical system
these libraries will already be in RAM. This gives ippusbxd a minimal ram
footprint.
2. Requires no read access to any files.
3. Ships with a strict apparmor profile.
3. Runs warning & leak free in valgrind
4. Compiles warning free in clang
5. Analyzed warning free in Coverity
6. Can be installed anywhere
7. Near zero cpu usage while idle
8. Low cpu usage while working

Building
=======

To build ippusbxd you must have the libusb 1.0 development headers installed along
with cmake.

Under Ubuntu and Debian this is acomplished by running:
  sudo apt-get install libusb-1.0-0-dev cmake

Once the dependencies are installed simply run:
  make

That will run a makefile which will inturn run cmake. This makefile also
supports several GNU-style make commands such as clean, and redep.

Presentation on IPPUSBXD
=======
On August 2014 at the Fall Printer Working Group meeting I gave a presentation
on ippusbxd and the ipp over usb protocol. Slides from this presentation can be
found in the docs folder.

IPPUSBXD, the name
=======
The original name for this project was ippusbd. Part way through development it
came to my attention that ippusbd was the name of the ipp over usb implemented
used by Mac OSX.

This prompted a rename and Ira of the OpenPrinting group and PWG suggested
IPPUSBXD.

Either all-caps IPPUSBXD or all-lower-case ippusbxd are valid names.

License
=======
Copyright 2014 Daniel Dressler

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
