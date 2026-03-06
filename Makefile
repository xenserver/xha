#
#   Top-level Makefile
#
#   This makefile installs the debug version (compiled without NDEBUG).
#

.PHONY: build clean debug

all: debug

debug:
	@mkdir -p debug
	$(MAKE) -C include DEFMAKE=default-debug.mk
	$(MAKE) -C lib DEFMAKE=default-debug.mk
	$(MAKE) -C daemon DEFMAKE=default-debug.mk
	$(MAKE) -C commands DEFMAKE=default-debug.mk
	$(MAKE) -C scripts DEFMAKE=default-debug.mk
	
clean: debug-clean

debug-clean:
	$(MAKE) -C include clean DEFMAKE=default-debug.mk
	$(MAKE) -C lib clean DEFMAKE=default-debug.mk
	$(MAKE) -C daemon clean DEFMAKE=default-debug.mk
	$(MAKE) -C command clean DEFMAKE=default-debug.mk
	$(MAKE) -C scripts clean DEFMAKE=default-debug.mk
	-rmdir debug
	
#here
install: debug-install

debug-install:
	@mkdir -p debug
	$(MAKE) -C include install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	$(MAKE) -C lib install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	$(MAKE) -C daemon install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	$(MAKE) -C commands install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	$(MAKE) -C scripts install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
