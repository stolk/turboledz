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

When a Turbo LEDz device is plugged in, and the daemon has not yet send any data to it, the device will show a wave on the display.
This gets replaced by statistical data on the CPU load (and later: Core frequencies) when the turboledzd process contacts it.
When the host PC goes to sleep, the device will show the wave again on its display.

## Known issues

## Copyright

turboledzd is (c)2021 by Bram Stolk and licensed under the GPL.

