
CFLAGS = -O2 -Wall -Wextra -std=c99 -pedantic
APPNAME = gamepad2key
LIBS = -lX11 -lXtst

.PHONY: all clean
all: $(APPNAME)

clean:
	$(RM) $(APPNAME)

$(APPNAME): main.c
	$(CC) -s $(CFLAGS) $^ -o $@ $(LIBS)
