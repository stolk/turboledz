# turboledz
Daemon to control Turbo LEDz devices.

![screenshot](images/screenshot0.png "screenshot")


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

## Building

To build the daemon, use:
```
$ make
```

To build a debian package, use:
```
$ make turboledz-1.0.deb
```

## Known issues

## Copyright

turboledzd is (c)2021 by Bram Stolk and licensed under the GPL.

