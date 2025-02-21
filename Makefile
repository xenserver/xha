#
#   Top-level Makefile
#
#   This makefile installs the debug version (compiled without NDEBUG).
#

.PHONY: build clean debug

all: debug

debug:
	@mkdir -p debug
	@cd include;  make DEFMAKE=default-debug.mk
	@cd lib;      make DEFMAKE=default-debug.mk
	@cd daemon;   make DEFMAKE=default-debug.mk
	@cd commands; make DEFMAKE=default-debug.mk
	@cd scripts;  make DEFMAKE=default-debug.mk
	
clean: debug-clean

debug-clean:
	@cd include;  make clean DEFMAKE=default-debug.mk
	@cd lib;      make clean DEFMAKE=default-debug.mk
	@cd daemon;   make clean DEFMAKE=default-debug.mk
	@cd commands; make clean DEFMAKE=default-debug.mk
	@cd scripts;  make clean DEFMAKE=default-debug.mk
	-rmdir debug
	
#here
install: debug-install

debug-install:
	@mkdir -p debug
	@cd include;  make install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd lib;      make install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd daemon;   make install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd commands; make install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
	@cd scripts;  make install DESTDIR=$(DESTDIR) DEFMAKE=default-debug.mk
