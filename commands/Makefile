#
#   commands/Makefile
#

TARGET_DIR=commands
DEFMAKE=default-debug.mk
include ../$(DEFMAKE)

TARGET  += $(OBJDIR)/calldaemon
TARGET  += $(OBJDIR)/writestatefile
TARGET  += $(OBJDIR)/dumpstatefile
TARGET  += $(OBJDIR)/cleanupwatchdog
TARGET  += $(OBJDIR)/weightctl

OBJS    += $(OBJDIR)/calldaemon.o
OBJS    += $(OBJDIR)/writestatefile.o
OBJS    += $(OBJDIR)/stubs.o
OBJS    += $(OBJDIR)/dumpstatefile.o
OBJS    += $(OBJDIR)/cleanupwatchdog.o
OBJS    += $(OBJDIR)/weightctl.o

all: $(OBJS) $(TARGET)

$(OBJDIR)/calldaemon: $(OBJS) $(HALIBS)
	$(CC) $(OBJDIR)/calldaemon.o $(HALIBS) $(LIBS) -o $@
	@chmod 0755 $@

$(OBJDIR)/writestatefile: $(OBJS) $(HALIBS)
	$(CC) $(OBJDIR)/writestatefile.o $(OBJDIR)/stubs.o $(HALIBS) $(LIBS) -o $@
	@chmod 0755 $@

$(OBJDIR)/dumpstatefile:$(OBJS) $(HALIBS)
	$(CC) $(OBJDIR)/dumpstatefile.o $(OBJDIR)/stubs.o $(HALIBS) $(LIBS) -o $@
	@chmod 0755 $@

$(OBJDIR)/cleanupwatchdog:$(OBJS) $(HALIBS)
	$(CC) $(OBJDIR)/cleanupwatchdog.o $(HALIBS) $(LIBS) -o $@
	@chmod 0755 $@

$(OBJDIR)/weightctl:$(OBJS) $(HALIBS)
	$(CC) $(OBJDIR)/weightctl.o $(HALIBS) $(LIBS) -o $@
	@chmod 0755 $@

install: $(TARGET)
	@mkdir -p $(DESTDIR)$(INSDIR)
	@cp $(TARGET) $(DESTDIR)$(INSDIR)

clean:
	rm -f $(TARGET) $(OBJS)

$(HALIBS):

$(OBJDIR)/calldaemon.o: calldaemon.c $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/writestatefile.o: writestatefile.c $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/stubs.o: stubs.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/dumpstatefile.o: dumpstatefile.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/cleanupwatchdog.o: cleanupwatchdog.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/weightctl.o: weightctl.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
