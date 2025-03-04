#
#   default-debug.mk
#

CC=gcc
SOURCEDIR=..
override CFLAGS+=-g -Wall -Wno-multichar -Werror=pointer-to-int-cast -Og

OBJDIR=$(SOURCEDIR)/debug

INCDIR=$(SOURCEDIR)/include
INCLUDES=-I$(INCDIR)
LIBS=-lxml2 -lrt $(LDFLAGS)
HALIBS=$(OBJDIR)/libxha.a
INSDIR=/usr/libexec/xapi/cluster-stack/xhad
LOGCONFDIR=/etc/logrotate.d

.PHONY: build clean debug

%.o: %.c $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $<
	
