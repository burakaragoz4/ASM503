# frequency makefile
#   make          -> frequency derlenir
#   make install  -> frequency, proje kokundeki bin/ dizinine kopyalanir
#   make clean    -> make ve make install ile olusan tum dosyalar silinir
#
# Ortak goruntu isleme kodu ../common/fpimg.c dosyasindadir; nesne dosyasi
# bu dizinde olusturulur.

PROG    = frequency
SRCS    = $(PROG).c fpimg.c
OBJS    = $(SRCS:.c=.o)
VPATH   = ../common

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c99 -pedantic -I../common
LDLIBS  = -lm

# Windows'ta (MSYS2/MinGW) gcc ciktiya .exe ekler
ifneq ($(filter Windows_NT,$(OS))$(findstring _NT,$(shell uname -s 2>/dev/null)),)
EXE     = .exe
endif

BINDIR  = ../../bin
TARGET  = $(PROG)$(EXE)

.PHONY: all install clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c fpimg.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	mkdir -p $(BINDIR)
	cp $(TARGET) $(BINDIR)/

clean:
	rm -f $(OBJS) $(TARGET) $(BINDIR)/$(TARGET)
