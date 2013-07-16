CC       = gcc
CFLAGS   = -g
INCLUDES = -I/usr/include/libxml2
LIBS     = -lxml2 -lpthread

TARGET   = ../cfread

OBJS=cf_read.o cf_read_test.o log.o

all:../cfread

../cfread: $(OBJS)
	cc $(OBJS) $(LIBS) -o $@
	
clean:
	-rm -f $(TARGET) $(OBJS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

