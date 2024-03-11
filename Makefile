AVRDUDE_MCPU=m168
LIBC_MCPU=atmega168
FCLK=8000000

PROGTYPE=stk500v2
PROGPORT=/dev/ttyACM0

LIBC=/home/simon/local/avr

OBJS=

# Default: 1MHz internal osc
#LFUSE=0x42
# External clock
LFUSE=0xD0

# JTAG disabled
HFUSE=0xD9
# Brown-out-detect disabled
EFUSE=0xFF

AVRDUDE=avrdude -p $(AVRDUDE_MCPU) -c $(PROGTYPE) -P $(PROGPORT) -B 16.0

CC=avr-gcc
CFLAGS=-g -Wall -O2 -mmcu=$(LIBC_MCPU) -DF_CPU=$(FCLK) -I$(LIBC)/include
#LDLIBS=-lgcc
LDFLAGS=--sysroot $(LIBC)

TARGET=mouse

$(TARGET).hex: $(TARGET).elf
	avr-objcopy -j .text -j .data -O ihex $^ $@

$(TARGET).elf: main.o $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

flash: $(TARGET).hex
	$(AVRDUDE) -U flash:w:$^:i

fuses:
	#$(AVRDUDE) -U efuse:w:$(EFUSE):m
	$(AVRDUDE) -U hfuse:w:$(HFUSE):m
	$(AVRDUDE) -U lfuse:w:$(LFUSE):m

clean:
	$(RM) *.o *.elf *.hex
