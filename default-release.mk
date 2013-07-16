#
#   default-release.mk
#

CC=gcc
SOURCEDIR=..
CFLAGS=-g -Wall -Wno-multichar

CFLAGS+=-DNDEBUG 
OBJDIR=$(SOURCEDIR)/release

INCDIR=$(SOURCEDIR)/include
INCLUDES=-I$(INCDIR)
LIBS=-lxml2 -lrt
HALIBS=$(OBJDIR)/libxha.a
INSDIR=/opt/xensource/xha
LOGCONFDIR=/etc/logrotate.d

.PHONY: build clean debug release

%.o: %.c $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $<
	
%.a: %.c $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $<
	@$(AR) rv $@ $*.o
	@$(RM) $*.o
