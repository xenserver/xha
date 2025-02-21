#
#   daemon/Makefile
#
TARGET_DIR=daemon
DEFMAKE=default-debug.mk
include ../$(DEFMAKE)

LIBS    += -pthread
LIBS    += -rdynamic
LIBS    += -ldl

VPATH=.

TARGET   = $(OBJDIR)/xhad
LST      = $(TARGET).lst

OBJS    +=$(OBJDIR)/bond_mon.o
OBJS    +=$(OBJDIR)/com.o
OBJS    +=$(OBJDIR)/heartbeat.o
OBJS    +=$(OBJDIR)/lock_mgr.o
OBJS    +=$(OBJDIR)/log.o
OBJS    +=$(OBJDIR)/main.o
OBJS    +=$(OBJDIR)/sc_sv.o
OBJS    +=$(OBJDIR)/sc_func.o
OBJS    +=$(OBJDIR)/sm.o
OBJS    +=$(OBJDIR)/statefile.o
OBJS    +=$(OBJDIR)/watchdog.o
OBJS    +=$(OBJDIR)/xapi_mon.o
OBJS    +=$(OBJDIR)/fist.o
OBJS    +=$(OBJDIR)/hostweight.o

all: $(TARGET) $(LST)

$(TARGET): $(OBJS) $(HALIBS)
	$(CC) -o $@ $(OBJS) $(HALIBS) $(LIBS)
	@chmod 0755 $@

$(LST): $(TARGET)
	objdump -CxS --prefix-addresses $(TARGET) > $@

install: $(TARGET)
	@mkdir -p $(DESTDIR)$(INSDIR)
	@cp $(TARGET) $(DESTDIR)$(INSDIR)

clean:
	rm -f $(TARGET) $(OBJS) $(LST)

$(OBJDIR)/com.o: com.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/bond_mon.o: bond_mon.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/heartbeat.o: heartbeat.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/lock_mgr.o: lock_mgr.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/log.o: log.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/main.o: main.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/sc_sv.o: sc_sv.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/sc_func.o: sc_func.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/sm.o: sm.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/statefile.o: statefile.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/watchdog.o: watchdog.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/xapi_mon.o: xapi_mon.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/fist.o: fist.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
$(OBJDIR)/hostweight.o: hostweight.c  $(INCDIR)/*.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

