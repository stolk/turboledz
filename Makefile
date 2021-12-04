#
# To build the turboledzd daemon and the turboledz debian package.
# (c)2021 by Game Studio Abraham Stolk Inc.
#

CC=cc

CFLAGS=-g -Wall -Wextra # -static

PKG=turboledz-1.0

daemon/turboledzd: daemon/turboledzd.c daemon/cpuinf.c daemon/cpuinf.h
	$(CC) $(CFLAGS) daemon/turboledzd.c daemon/cpuinf.c -o daemon/turboledzd -lhidapi-hidraw -ludev

$(PKG).deb: daemon/turboledzd
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
	mkdir -p $(PKG)/DEBIAN
	cp ./DEBIAN/* $(PKG)/DEBIAN/
	sudo chown root:root -R $(PKG)
	dpkg -b $(PKG)

package: $(PKG).deb

clean:
	rm -f $(PKG).deb
	rm -f daemon/turboledzd

