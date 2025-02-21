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
	@cd include;  $(MAKE) clean DEFMAKE=default-debug.mk
	@cd lib;      $(MAKE) clean DEFMAKE=default-debug.mk
	@cd daemon;   $(MAKE) clean DEFMAKE=default-debug.mk
	@cd commands; $(MAKE) clean DEFMAKE=default-debug.mk
	@cd scripts;  $(MAKE) clean DEFMAKE=default-debug.mk
	-rmdir debug
	
#here
install: debug-install

debug-install:
	@mkdir -p debug
	@cd include;  $(MAKE) install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd lib;      $(MAKE) install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd daemon;   $(MAKE) install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd commands; $(MAKE) install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd scripts;  $(MAKE) install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
