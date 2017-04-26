# IPPUSBXD [![Coverity analysis status](https://scan.coverity.com/projects/2634/badge.svg)](https://scan.coverity.com/projects/2634)

Version 1.23

About
=======
IPPUSBXD is a userland driver for IPP-over-USB class USB devices. It has been
designed for Linux but uses a cross platform usb library allowing eventual
porting to Windows and other non-POSIX platforms.

The IPP-over-USB standard was ratified by the USB forum in 2012. As of
2014 Mac OS X implemented this standard and with the addition of
ippusbxd Linux shall as well.

IPPUSBXD depends on POSIX threads, POSIX networking, and libusb as
developed by the community at libusb.info

IPPUSBXD has the following advantages;

1. At runtime links only with libc, pthreads, libusb, and
libavahi*. On a typical system these libraries will already be in
RAM. This gives ippusbxd a minimal ram footprint.
2. Requires no read access to any files.
3. Ships with a strict AppArmor profile.
3. Runs warning & leak free in valgrind
4. Compiles warning free in clang
5. Analyzed warning free in Coverity
6. Can be installed anywhere
7. Near zero CPU usage while idle
8. Low CPU usage while working

Building
=======

To build ippusbxd you must have the development headers of libusb 1.0,
libavahi-common, and libavahi-client installed along with cmake.

Under Ubuntu and Debian:
  sudo apt-get install libusb-1.0-0-dev libavahi-common-dev libavahi-client-dev cmake

Under Fedora:
  sudo yum install libusbx-devel.* cmake

  Install also the *-devel packages of libavahi-common and libavahi-client

Once the dependencies are installed simply run:
  make

That will run a makefile which will in turn run cmake. This makefile
also supports several GNU-style make commands such as clean, and
redep.

Installation on a system with systemd, UDEV, and cups-filters
=======

Most systems nowadays use systemd for starting up all system services
(instead of System V "init", as PID 1), UDEV to automatically set up
hardware added to the system while it is running, and cups-filters to
provide non-Mac-OS filters, backends, and cups-browsed. Therefore we
explain only a method using systemd and UDEV here.

In these systems it is recommended to start ippusbxd via systemd when
an appropriate printer is connected and discovered by UDEV.
cups-filters from version 1.13.2 on and CUPS from version 2.2.2 has
everything needed for driverless printer setup. "driverless" means
that no printer driver, with the driver being any software or data
specific to (a) certain printer model(s) is needed. driverless
printing makes use of IPP to allow the client to query the printer's
capabilities and IPP-over-USB was developed to allow these queries
also if the printer is not on the network but connected via USB.
Therefore we can assume that all IPP-over-USB printers support
driverless printing.

A remark to driverless printing: There are two very similar standards:
AirPrint, a proprietary standard from Apple and IPP Everywhere, an
open standard of the Printer Working Group (PWG, http://www.pwg.org/).
Both use the same methods of DNS-SD broadcasting of network printers,
IPP-over-USB via the USB interface class 7, subclass 1, protocol 4,
and IPP 2.0 with all its attributes for querying of capabilities,
sending jobs with options as IPP attributes, and monitoring the status
of the printer. The only difference is that IPP Everywhere uses PWG
Raster as its raster data format and AirPrint uses Apple Raster. Even
these raster formats are very similar. Therefore CUPS and CUPS filters
simply support both methods.

Note that these instructions and the sample files are tested on
Ubuntu. On other distributions there are perhaps some changes needed,
for example of the directories where to place the file and of paths in
the files.

There are two methods to install ippusbxd, one exposing the printer
on localhost. and one exposing the printer on the dummy0 interface.

Exposing the printer on localhost is the way how the IPP-over-USB
standard is intended and therefore this how it is intended to proceed
on production system and especially on Linux distributions.
Disadvantage of this method is that Avahi needs to be modified so that
it advertises services on localhost and these only on the local
machine.

Exposing the printer on the dummy0 interface does not require any
changes on Avahi, but it is more awkward to set up the syetm and to
access the printer and its web administration interface.

1. Expose the printer on localhost
----------------------------------

First, install ippusbxd:

sudo cp exe/ippusbxd /usr/sbin

Make sure that this file is owned by root and world-readable and
-executable.

Now install the files to manage the automatic start of ippusbxd:

sudo cp systemd-udev/55-ippusbxd.rules /lib/udev/rules.d/
sudo cp systemd-udev/ippusbxd@.service /lib/systemd/system/

Make sure that they are owned by root and world-readable.

Why do we not start ippusbxd directly out of the UDEV rules file?

If we would do so, UDEV would kill ippusbxd after a timeout of 5
minutes. Out of UDEV rules you can only start programs which do not
need to keep running permanently, like daemons. Therefore we use
systemd here.

Apply the following patch to the source code of Avahi:

----------
--- avahi-core/iface-linux.c~	2016-02-09 04:12:59.295979998 -0200
+++ avahi-core/iface-linux.c	2017-04-13 11:25:52.103355254 -0300
@@ -104,8 +104,8 @@
         hw->flags_ok =
             (ifinfomsg->ifi_flags & IFF_UP) &&
             (!m->server->config.use_iff_running || (ifinfomsg->ifi_flags & IFF_RUNNING)) &&
-            !(ifinfomsg->ifi_flags & IFF_LOOPBACK) &&
-            (ifinfomsg->ifi_flags & IFF_MULTICAST) &&
+            ((ifinfomsg->ifi_flags & IFF_LOOPBACK) ||
+             (ifinfomsg->ifi_flags & IFF_MULTICAST)) &&
             (m->server->config.allow_point_to_point || !(ifinfomsg->ifi_flags & IFF_POINTOPOINT));
 
         /* Handle interface attributes */
--- avahi-core/iface-pfroute.c~	2015-04-01 01:58:14.149727123 -0300
+++ avahi-core/iface-pfroute.c	2017-04-13 11:28:47.547016465 -0300
@@ -80,8 +80,8 @@
   hw->flags_ok =
     (ifm->ifm_flags & IFF_UP) &&
     (!m->server->config.use_iff_running || (ifm->ifm_flags & IFF_RUNNING)) &&
-    !(ifm->ifm_flags & IFF_LOOPBACK) &&
-    (ifm->ifm_flags & IFF_MULTICAST) &&
+    ((ifm->ifm_flags & IFF_LOOPBACK) ||
+     (ifm->ifm_flags & IFF_MULTICAST)) &&
     (m->server->config.allow_point_to_point || !(ifm->ifm_flags & IFF_POINTOPOINT));
 
   avahi_free(hw->name);
@@ -427,8 +427,8 @@
         hw->flags_ok =
             (flags & IFF_UP) &&
             (!m->server->config.use_iff_running || (flags & IFF_RUNNING)) &&
-            !(flags & IFF_LOOPBACK) &&
-            (flags & IFF_MULTICAST) &&
+            ((flags & IFF_LOOPBACK) ||
+             (flags & IFF_MULTICAST)) &&
             (m->server->config.allow_point_to_point || !(flags & IFF_POINTOPOINT));
         hw->name = avahi_strdup(lifreq->lifr_name);
         hw->mtu = mtu;
----------

Build and install Avahi. This makes Avahi not only advertising
services on the usual network interfaces but also on the "lo"
(loopback) interface (localhost). The services on the loopback
interface (on localhost) are only advertised on the local machine, so
no additional info gets exposed to the network, especially no
local-only service gets shared by this.

This works well as long as your machine is connected to some kind of
network (does not necessarily need to be a connection to the Internet,
a virtual network interface to virtual machines running locally is
enough). It is possible that the advertising of the printer stops if
the loopback interface is the only network interface running due to
lack of a multicast-capable interface.

Now we can restart systemd and UDEV to activate all this:

sudo systemctl daemon-reload
sudo systemctl restart udev

If we connect and turn on an IPP-over-USB printer, ippusbxd gets
started and makes the printer available under the IPP URI

ipp://localhost:60000/ipp/print

and its web administration interface under

http://localhost:60000/

(if you have problems with the Chrome browser, use Firefox).

It is also DNS-SD-broadcasted via our modified Avahi on the lo interface.

To set up a print queue you could simply run

lpadmin -p printer -E -v ipp://localhost:60000/ipp/print -meverywhere

The "-meverywhere" makes CUPS auto-generate the PPD file for the
printer, based on an IPP query of the printer's capabilities,
independent whether the printer is an IPP Everywhere printer or an
AirPrint printer.

To create a print queue with the web interface of CUPS
(http://localhost:631/), look for your printer under the discovered
network printers (CUPS does not see that it is USB in reality) and
select the entry which contains "driverless". On the page to select
the models/PPDs/drivers, also select the entry containing
"driverless". Then complete the setup as ususal.

The best solution is to let cups-browsed auto-create a print queue
when the printer gets connected and remove it when the printer gets
turned off or disconnected (do not worry about option settings,
cups-browsed saves them).

To do so, edit /etc/cups/cups-browsed.conf making sure that there is a
line

CreateIPPPrinterQueues driverless

or

CreateIPPPrinterQueues all

and no other line beginning with

CreateIPPPrinterQueues

After editing the file restart cups-browsed with

sudo systemctl stop cups-browsed
sudo systemctl start cups-browsed

Now you have a print queue whenever the printer is available and no
print queue cluttering your print dialogs when the printer is not
available.

2. Expose the printer on the dummy0 interface
---------------------------------------------

First, install ippusbxd:

sudo cp exe/ippusbxd /usr/sbin

Make sure that this file is owned by root and world-readable and
-executable.

Now install the files to manage the automatic start of ippusbxd:

sudo cp systemd-udev/55-ippusbxd.rules /lib/udev/rules.d/
sudo cp systemd-udev/ippusbxd@.service.dummy0 /lib/systemd/system/ippusbxd@.service

Make sure that they are owned by root and world-readable.

Now create a "dummy0" network interface:

sudo modprobe dummy
sudo ifconfig dummy0 10.0.0.1 netmask 255.255.255.0 multicast
sudo ifconfig dummy0 up multicast

You could put these commands into /etc/rc.local to run them
automatically at boot.

Why not simply use "localhost" with the always available loopback
("lo") interface?

We want that our IPP-over-USB printer appears to our system like a
network printer, so that CUPS and cups-browsed auto-detect it with the
usual methods so that we can easily set up a print queue, even fully
automatically, and that we can use CUPS' IPP backend to send print
jobs to our printer.

If we use "localhost", we can access the printer with the IPP CUPS
backend and also access its web administration interface with a web
browser, but the printer cannot get auto-discovered by cups-browsed or
by CUPS backends like dnssd or driverless, making it awkward to create
a print queue for the printer. This is because the loopback interface
(which provides "localhost") is not multicast-capable and therefore
cannot get DNS-SD-broadcasted by Avahi.

Now one could think why not simply use the standard network interface
"eth0" or "wlan0"? The problem here is that the printer gets
broadcasted and accessible in the whole local network, so we share our
USB printer and do not want it. In addition, if our computer is not
connected to a network, these interfaces are not available.

"dummy0" is always local-only but does multicast and therefore gets
DNS-SD-broadcasted by Avahi, and that only on the local machine. So
we have the full emulation of a driverless network printer only on our
local machine, as we want a USB printer only be available on our local
machine.

Now we can restart systemd and UDEV to activate all this:

sudo systemctl daemon-reload
sudo systemctl restart udev

If we connect and turn on an IPP-over-USB printer, ippusbxd gets
started and makes the printer available under the IPP URI

ipp://10.0.0.1:60000/ipp/print

and its web administration interface under

http://10.0.0.1:60000/

(if you have problems with the Chrome browser, use Firefox).

It is also DNS-SD-broadcasted via Avahi on the dummy0 interface.

To set up a print queue you could simply run

lpadmin -p printer -E -v ipp://10.0.0.1:60000/ipp/print -meverywhere

The "-meverywhere" makes CUPS auto-generate the PPD file for the
printer, based on an IPP query of the printer's capabilities,
independent whether the printer is an IPP Everywhere printer or an
AirPrint printer.

To create a print queue with the web interface of CUPS
(http://localhost:631/), look for your printer under the discovered
network printers (CUPS does not see that it is USB in reality) and
select the entry which contains "driverless". On the page to select
the models/PPDs/drivers, also select the entry containing
"driverless". Then complete the setup as ususal.

The best solution is to let cups-browsed auto-create a print queue
when the printer gets connected and remove it when the printer gets
turned off or disconnected (do not worry about option settings,
cups-browsed saves them).

To do so, edit /etc/cups/cups-browsed.conf making sure that there is a
line

CreateIPPPrinterQueues driverless

or

CreateIPPPrinterQueues all

and no other line beginning with

CreateIPPPrinterQueues

After editing the file restart cups-browsed with

sudo systemctl stop cups-browsed
sudo systemctl start cups-browsed

Now you have a print queue whenever the printer is available and no
print queue cluttering your print dialogs when the printer is not
available.


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
Copyright 2014 Daniel Dressler,
          2015-2016 Till Kamppeter

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
