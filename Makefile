# See LICENSE for Copyright etc.

MCU			=		attiny861

LIBNAME		=		libusitwislave
OBJFILES	=		$(LIBNAME).o
LIBRARY		=		$(LIBNAME).a
CFLAGS		=		-Wall -Winline -Os -mmcu=$(MCU) -DF_CPU=8000000UL -DUSI_ON_PORT_A
LDFLAGS		=		-Wall -mmcu=$(MCU)

.PHONY:				all clean
.SUFFIXES:
.SUFFIXES:			.a .c .o .elf
.PRECIOUS:			.c .h

all:				$(LIBRARY)

libusitwislave.o:	libusitwislave.c usitwislave.h usitwislave_devices.h

%.o:				%.c
					avr-gcc -c $(CFLAGS) $< -o $@

$(LIBRARY):			$(OBJFILES)
					@-rm -f $@ > /dev/null || true
					ar rc $@ $^

clean:			
					@echo rm $(OBJFILES) $(ELFFILE) $(LIBRARY)
					@-rm $(OBJFILES) $(ELFFILE) $(LIBRARY) 2> /dev/null || true
