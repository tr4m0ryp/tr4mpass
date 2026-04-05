CC       = cc
CFLAGS   = -Wall -Wextra -std=c99 -D_GNU_SOURCE -O2
LDFLAGS  =

# Library discovery via pkg-config (warn but do not fail if missing)
PKG_LIBS = libimobiledevice-1.0 libirecovery-1.0 libusb-1.0 \
           libplist-2.0 openssl

CFLAGS  += $(shell pkg-config --cflags $(PKG_LIBS) 2>/dev/null)
LDFLAGS += $(shell pkg-config --libs   $(PKG_LIBS) 2>/dev/null)

# Auto-discover all C sources under src/
SRCS     = $(shell find src -name '*.c')
OBJS     = $(SRCS:.c=.o)
TARGET   = tr4mpass

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

clean:
	find src -name '*.o' -delete
	rm -f $(TARGET)

.PHONY: all clean
