# raw2bmp makefile
#   make          -> raw2bmp derlenir
#   make install  -> raw2bmp, proje kokundeki bin/ dizinine kopyalanir
#   make clean    -> make ve make install ile olusan tum dosyalar silinir

PROG    = raw2bmp
SRCS    = raw2bmp.c
OBJS    = $(SRCS:.c=.o)

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c99 -pedantic
LDFLAGS =

# Windows'ta (MSYS2/MinGW) gcc ciktiya .exe ekler
ifneq ($(filter Windows_NT,$(OS))$(findstring _NT,$(shell uname -s 2>/dev/null)),)
EXE     = .exe
endif

BINDIR  = ../../bin
TARGET  = $(PROG)$(EXE)

.PHONY: all install clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	mkdir -p $(BINDIR)
	cp $(TARGET) $(BINDIR)/

clean:
	rm -f $(OBJS) $(TARGET) $(BINDIR)/$(TARGET)
