
CFLAGS = -O2 -Wall -Wextra -std=c99 -pedantic
APPNAME = gamepad2key
NODEV = 0
ifeq ($(NODEV), 1)
CFLAGS += -IX11mini
LIBDIR = /usr/lib/x86_64-linux-gnu
LIBS = $(LIBDIR)/libX11.so.6 $(LIBDIR)/libXtst.so.6
else
LIBS = -lX11 -lXtst
endif

.PHONY: all clean
all: $(APPNAME)

clean:
	$(RM) $(APPNAME)

$(APPNAME): main.c
	$(CC) -s $(CFLAGS) $^ -o $@ $(LIBS)
