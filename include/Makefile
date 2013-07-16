#
#   include/Makefile
#

TARGET_DIR=include
DEFMAKE=default-debug.mk
include ../$(DEFMAKE)

TARGET   = mtcerrno.h
TARGET   += buildid.h

all: $(TARGET)

mtcerrno.h: errdef.awk mtcerrno.def 
	gawk -f errdef.awk mtcerrno.def > $@

.PHONY: buildid.h
buildid.h:
	sh buildid.sh > $@


install: $(TARGET)

clean:
	rm -f $(TARGET)
