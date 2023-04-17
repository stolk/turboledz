#
# To build the turboledzd daemon and the turboledz debian package.
# (c)2021 by Game Studio Abraham Stolk Inc.
#

CC=cc

CFLAGS=-g -O2 -Wall -Wextra -DSUPPORT_ODO # -static

PKG=turboledz-1.3

daemon/turboledzd: daemon/turboledzd.c daemon/cpuinf.c daemon/cpuinf.h daemon/turboledz.h daemon/turboledz.c
	$(CC) $(CFLAGS) daemon/turboledzd.c daemon/turboledz.c daemon/cpuinf.c -o daemon/turboledzd -lhidapi-hidraw -ludev

simulator/turboledzsim: daemon/cpuinf.c daemon/cpuinf.h simulator/grapher.c simulator/grapher.h simulator/turboledzsim.c
	$(CC) $(CFLAGS) -Idaemon/ daemon/cpuinf.c simulator/grapher.c simulator/turboledzsim.c -o simulator/turboledzsim

$(PKG).deb: daemon/turboledzd daemon/manpage
	sudo rm -rf ./$(PKG)
	mkdir -p $(PKG)/etc
	cp ./daemon/conf $(PKG)/etc/turboledz.conf
	mkdir -p $(PKG)/usr/share/man/man1
	cp ./daemon/manpage $(PKG)/usr/share/man/man1/turboledzd.1
	gzip $(PKG)/usr/share/man/man1/turboledzd.1
	mkdir -p $(PKG)/usr/lib/udev/rules.d
	cp ./udev/*.rules $(PKG)/usr/lib/udev/rules.d/
	mkdir -p $(PKG)/usr/bin
	cp ./daemon/turboledzd $(PKG)/usr/bin/
	mkdir -p $(PKG)/lib/systemd/system
	cp ./systemd/turboledz.service $(PKG)/lib/systemd/system/
	mkdir -p $(PKG)/lib/systemd/system-sleep
	cp ./systemd/turboledz.sleep $(PKG)/lib/systemd/system-sleep/
	mkdir -p $(PKG)/var/lib/turboledz
	mkdir -p $(PKG)/DEBIAN
	cp ./DEBIAN/* $(PKG)/DEBIAN/
	sudo chown root:root -R $(PKG)
	sudo chown daemon.daemon $(PKG)/var/lib/turboledz
	dpkg -b $(PKG)

package: $(PKG).deb

descriptorreport:
	usbhid-dump -m 2341:8037 | grep -v : | xxd -r -p | hidrd-convert -o spec


clean:
	rm -f $(PKG).deb
	rm -f daemon/turboledzd

