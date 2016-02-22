DLKM=byd-0.2

INSTALL_SRC_DIR = $(DESTDIR)/usr/src/psmouse-$(DLKM)

install:
	install -d $(INSTALL_SRC_DIR)
	cp -r src $(INSTALL_SRC_DIR)
	install -m 0644 dkms.conf $(INSTALL_SRC_DIR)

build-install:
	./install.sh
