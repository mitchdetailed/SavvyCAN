Adding a new connection
========================

![New Connection](./images/NewConnection.png)

At the moment it is possible to use any QT SerialBus compatible device and any GVRET compatible device in any of the supported operating systems. SerialBus supports socketcan on linux, passthrough on Linux and Windows 32 bit, and Vector, PeakCAN, and TinyCAN on supported OS's.

At this time GVRET compatible devices are: EVTVDue, EVTV CANDue (1.3/2/2.1/2.2), 
Teensy 3.1-3.6, Macchina M2, Macchina A0, EVTV ESP32 Due.

You can also use a variety of network based connections to gain access to remote capture hardware.

Finding your device
===================

Next to the port box there is a "Scan" button. It asks the driver for the connection type you have
selected which devices are actually attached and fills the list with what it finds, showing a readable
name for each one. This saves having to know a channel number or a serial number up front, and it
doubles as a quick way to check that a vendor driver is installed - if the driver is missing or no
hardware is plugged in, the scan says so instead of leaving you guessing.

Scanning works for gs_usb, CANalyst-II, Kvaser and IXXAT devices. gs_usb and CANalyst-II are scanned
automatically as soon as you pick them because looking at the USB bus is quick and needs no vendor
driver. Kvaser and IXXAT wait for you to press Scan since they have to load the vendor library first.
Serial port based connections list the machine's serial ports as they always have, and Scan just
refreshes that list. The remaining connection types have no way to enumerate anything, so their port
still has to be typed in.


Connecting To GVRET Devices
==============================

SavvyCAN is able to connect to GVRET compatible devices to capture new traffic. These 
devices will present as serial ports on the connected PC. To connect to a dongle select "Serial Connection." This will bring up a list of serial ports on the machine. Select the proper one and then press "Create New Connection". This will close the window and bring you back to the connection manager window. If connection succeeds the status will show "Connected" for your newly set up device.

You can also connect to some GVRET devices over the network (A0, EVTV ESP32Due). These devices broadcast their address. Once you've selected "Network Connection (GVRET)" you should see a list of IP addresses that appear to have GVRET devices on them. You can also manually enter the proper IP address but if the device did not automatically register itself it is unlikely to work with a manual entry either.


Connecting to QT SerialBus Compatible Devices
=============================================

SavvyCAN can also connect to a wide variety of CAN hardware through the built-in QT
SerialBus drivers. These drivers vary by operating system but support socketcan on LINUX
and Vector tools on both LINUX and Windows. When you select "QT SerialBus Devices" you will
get a list of device types supported. Select a device type and for most devices you should see
the Port list fill out with all registered and valid ports for that driver. Socketcan devices, for
instance, are automatically detected now. Then push "Create New Connection" and you should 
see the new connection in the table on the left of the window. Note that SocketCAN devices 
don't support changing the baud rate within a program. You must do this when you set up 
the connection via console commands. This is outside the scope of this documentation. 
Consult the SocketCAN documentation for details on configuring such devices.

QT also includes a "virtualcan" device type. You can use this to create a bus that will loop back anything you send to it. This is useful for testing without needing to connect any devices or load any log files.

Connecting to Socketcand
========================

This is a LINUX only solution which allows one to connect to a socketcan device that is registered on the local network. You can also set up SSH tunnels or VPN to expand the reach over the internet. It should fill out a list of any available socketcand interfaces. Setting up socketcand is outside the scope of this help file but may your GoogleFu be strong.

Connecting to gs_usb / candleLight adapters
===========================================

The "GSUSB (Candlelight)" type covers any adapter running the gs_usb firmware, which is a large
family: candleLight, CANable, the CES CANext FD and the ABE CAN Debugger among others. SavvyCAN
recognises the same set of USB IDs the Linux gs_usb driver does, and lists everything it finds when
you select this connection type, so if you have two adapters plugged in you can pick between them.
Each is identified by its serial number where the adapter reports one, and by its position on the USB
bus where it doesn't. Leaving the port empty means "use whichever adapter you find", which is what
connections saved by older versions of SavvyCAN do.

USB adapters with open protocols
================================

These adapters speak documented protocols straight over a serial port or USB, with no vendor driver
to install. The protocol implementations match the ones in the python-can project so traffic and logs
line up if you use both tools.

"Seeed USB-CAN Analyzer" - the small Seeed Studio analyzer. It appears as a serial port. The USB side
runs at 2Mbps by default, which is what the Serial Speed box is preset to, and you pick the CAN speed
separately. Leave the serial speed alone unless you have reflashed the adapter.

"Robotell CAN-USB" - appears as a serial port, normally at 115200. The CAN speed you select is
written into the adapter's configuration register when the connection comes up.

"CANalyst-II" - the two channel ControlCAN adapter, driven directly over USB. Both channels show up
as buses 0 and 1 in SavvyCAN and each can have its own speed set in the connection window. Pick the
adapter from the list, which is filled in for you when you select this connection type.

Connecting through vendor drivers
=================================

Several manufacturers only expose their hardware through their own driver library, which cannot be
shipped with SavvyCAN. For all of the connection types in this section you must install the vendor's
driver package first. SavvyCAN then loads that library when you connect - if it isn't installed the
connection reports Not Connected and says why in the debug output, so check there first if a
connection refuses to come up.

"Kvaser (CANlib)" - install Kvaser's driver package. The Port field is the CANlib channel number, so 0
is the first channel. Kvaser's virtual channels work too, which is a convenient way to try things
without hardware.

"IXXAT (VCI)" - install IXXAT's VCI driver. The Port field is "device:channel", both counted from
zero, so "0:0" is the first channel of the first adapter and "0:1" is its second channel.

"USB2CAN (CANAL)" - for 8devices USB2CAN adapters on Windows, which reach the adapter through
usb2can.dll. The Port field is the adapter's serial number, which is printed on the device. On Linux
these adapters are ordinary SocketCAN interfaces, so use a QT SerialBus connection there instead.
Note that CANAL fixes the bit rate when the adapter is opened, so changing the bus speed later makes
SavvyCAN close and reopen the adapter.

"isCAN (Thorsis)" - for Thorsis / ifak isCAN adapters, through iscandrv. The Port field is the channel
number. As with USB2CAN the speed is set at initialisation time, so changing it reinitialises the
channel.

"NI-CAN" - National Instruments' older CAN stack. The Port field is the NI-CAN interface name rather
than a number, so it looks like "CAN0". If your NI hardware uses NI-XNET rather than NI-CAN this
connection type will not find it.

"Neousys (WDT_DIO)" - the CAN ports built into Neousys industrial PCs. The Port field is the port
number. This driver is push based rather than polled: the library delivers frames on its own thread
and SavvyCAN hands them over to its capture thread, so nothing else needs configuring.

Talking to python-can
=====================

Two of the connection types exist purely to exchange traffic with python-can scripts rather than to
drive hardware.

"python-can over serial" implements python-can's `serial` backend framing. Point both ends at a serial
port, or at the two ends of a virtual serial pair, and frames flow both ways. There is no CAN speed to
choose because whatever is on the far end owns the bus.

"python-can UDP multicast" implements python-can's `udp_multicast` backend, where frames are msgpack
encoded and sent to a multicast group. Every tool that joins the group sees everything, so several
programs on one machine (or on a LAN segment) can share one virtual bus. The default is python-can's
IPv4 group, 239.74.163.2 on port 43113; the drop down also offers python-can's IPv6 default for when
you need to match a script that hasn't been told otherwise. SavvyCAN filters out the copies of its own
frames that multicast loops back, so you won't see your transmissions twice.

Connecting over MQTT
====================

Lastly, it is possible to connect to an MQTT broker to send and receive CAN traffic over the internet. This is much like socketcand but more cross platform and also supports easy broadcasting. For instance, for capture the flag events, it would be possible to connect the device over MQTT and have multiple participants and/or watchers all connected at once. Connection to the MQTT broker is set up in the main SavvyCAN preferences. In this window you merely select the topic name to subscribe to. There is currently no automatic way to list these topics so you will need to know the topic to subscribe to ahead of time. It should be noted that the bidirectional nature of this interface means that everyone is on equal footing. You can create an MQTT interface that others can connect to or you can connect to a topic that is currently being sent to from elsewhere and get the traffic. Additionally, the SavvyCAN source code at GitHub has a python script which can be used to connect a socketcan interface to MQTT. You can use this script on a remote system to connect it to the internet so that you can run SavvyCAN somewhere apart from the device under test.
