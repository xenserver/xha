#
#   Top-level Makefile
#
#   This makefile installs the debug version (compiled without NDEBUG).
#   To install release version, do "make release-install" or
#   modify this makefile to do release install by default. See #here below. 
#

.PHONY: build clean debug release

all: debug release

debug:
	@mkdir -p debug
	@cd include;  make DEFMAKE=default-debug.mk
	@cd lib;      make DEFMAKE=default-debug.mk
	@cd daemon;   make DEFMAKE=default-debug.mk
	@cd commands; make DEFMAKE=default-debug.mk
	@cd scripts;  make DEFMAKE=default-debug.mk
	
release:
	@mkdir -p release
	@cd include;  make DEFMAKE=default-release.mk
	@cd lib;      make DEFMAKE=default-release.mk
	@cd daemon;   make DEFMAKE=default-release.mk
	@cd commands; make DEFMAKE=default-release.mk
	@cd scripts;  make DEFMAKE=default-release.mk

clean: debug-clean release-clean

debug-clean:
	@cd include;  make clean DEFMAKE=default-debug.mk
	@cd lib;      make clean DEFMAKE=default-debug.mk
	@cd daemon;   make clean DEFMAKE=default-debug.mk
	@cd commands; make clean DEFMAKE=default-debug.mk
	@cd scripts;  make clean DEFMAKE=default-debug.mk
	-rmdir debug
	
release-clean:
	@cd include;  make clean DEFMAKE=default-release.mk
	@cd lib;      make clean DEFMAKE=default-release.mk
	@cd daemon;   make clean DEFMAKE=default-release.mk
	@cd commands; make clean DEFMAKE=default-release.mk
	@cd scripts;  make clean DEFMAKE=default-release.mk
	-rmdir release

#here
install: debug-install

debug-install:
	@mkdir -p debug
	@cd include;  make install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd lib;      make install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd daemon;   make install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd commands; make install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd scripts;  make install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk

release-install:
	@mkdir -p release
	@cd include;  make install DESTDIR=$(DESTDIR) DEFMAKE=default-release.mk
	@cd lib;      make install DESTDIR=$(DESTDIR) DEFMAKE=default-release.mk
	@cd daemon;   make install DESTDIR=$(DESTDIR) DEFMAKE=default-release.mk
	@cd commands; make install DESTDIR=$(DESTDIR) DEFMAKE=default-release.mk
	@cd scripts;  make install DESTDIR=$(DESTDIR) DEFMAKE=default-release.mk
