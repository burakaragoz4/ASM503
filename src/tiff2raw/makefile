# tiff2raw makefile
#   make          -> tiff2raw derlenir
#   make install  -> tiff2raw, proje kokundeki bin/ dizinine kopyalanir
#   make clean    -> make ve make install ile olusan tum dosyalar silinir

PROG    = tiff2raw
SRCS    = tiff2raw.c
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
