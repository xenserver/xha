#
#   lib/Makefile
#

TARGET_DIR=lib
DEFMAKE=default-debug.mk
include ../$(DEFMAKE)

INCLUDES    += -I/usr/include/libxml2

OBJS    +=statefileio.o
OBJS    +=config.o
OBJS    +=error.o
OBJS    +=weightio.o

TARGET=$(HALIBS)

$(HALIBS): $(OBJS)
	@$(AR) rv $@ $(OBJS)

all: $(TARGET)

install: $(TARGET)

clean:
	rm -f $(HALIBS) $(OBJS)
