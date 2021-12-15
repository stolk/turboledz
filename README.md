# turboledz
Daemon to control Turbo LEDz devices.

![model108](images/model108.jpeg "model108")


## Introduction

With the turboledzd daemon, you can display OS statistics, like CPU load, on a Turbo LEDz USB device.

The daemon is typically started as a system service using:

```
$ sudo systemctl start turboledz
```

To see the configuration options, do:
```
$ man turboledzd
```

The turboledzd process is written in C and uses very little resources itself.
I've measured it to be sub 0.5% of a single core, for the non-optimized debug binary.
An optimized build uses even less.

When a Turbo LEDz device is plugged in, and the daemon has not yet send any data to it, the device will show a wave on the display.
This gets replaced by statistical data on the CPU load (and later: Core frequencies) when the turboledzd process contacts it.
When the host PC goes to sleep, the device will show the wave again on its display.

## Dependencies

This daemon depends on two system libraries. Before building, make sure you have those  installed:
```
sudo apt install libudev-dev libhidapi-dev
```

## Building

To build the daemon, use:
```
$ make
```

To build and install a debian package, use:
```
$ make turboledz-1.0.deb
$ sudo dpkg -i turboledz-1.0.deb
```

## Running

Turbo LEDz devices show up a rawhid devices in `/dev/hidrawX` which need to have user access `rwx`.
This can be automatically set with a udev rule.

You can run turboledzd straight from the command-line, as user, to test.
The Debian package will set up a systemd service, and run the process under the daemon user.

## Plugging In

Even though a device can be plugged in with a regular USB to Micro-USB cable, installing the device in a 5.25" drive bay is done best with a 5-pin USB cable.
Locate the USB connectors on your motherboard, which are typically 9-pin or 5-pin.
For a 9-pin connector, you can choose either the top-row or bottom-row.
Depicted below is my motherboard with a 9-pin double port and a 4/5-pin single port.
You can use one of three to connect the device: either the red, orange or yellow annotated row.
Check your motherboard manual for orientation: The shielded cable goes to the unconnected pin-position.

![usbports](images/usbports.jpeg "usbports")

![usbconnector](images/usbconnector.jpeg "usbconnector")


## Known issues

## Copyright

turboledzd is (c)2021 by Bram Stolk and licensed under the GPL.

