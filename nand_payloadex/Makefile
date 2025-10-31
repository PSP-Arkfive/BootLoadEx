PSPSDK = $(shell psp-config --pspsdk-path)
PSPDEV = $(shell psp-config --pspdev-path)
PYTHON = $(shell which python3)
ARKSDK = $(PSPDEV)/share/ark-dev-sdk
BOOTLOADEX ?= ..

TARGET = nand_payloadex.bin

all: $(TARGET)

INCDIR = $(PSPSDK)/include $(PSPSDK)/include/iplsdk
CFLAGS = -std=c99 -Wall -Os -G0 -fno-pic $(addprefix -I, $(INCDIR)) -DPAYLOADEX

CC = psp-gcc
LD = psp-ld
STRIP = psp-strip
OBJCOPY = psp-objcopy
LINKFILE = $(BOOTLOADEX)/linkfile.l

REBOOT_OBJS = \
	main.o \
	pspbtcnf.o \
	patches_psp.o
	
C_OBJS = \
	$(BOOTLOADEX)/scanner.o \

LIBS = -L $(PSPSDK)/lib -lansic

ifdef DEBUG
CFLAGS += -DDEBUG=$(DEBUG)
LIBS += -lcolordebugger
endif

crt0.o : $(BOOTLOADEX)/crt0.S
	$(CC) -I$(PSPSDK)/include -c $< -o crt0.o

$(REBOOT_OBJS) : %.o : $(BOOTLOADEX)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

main.elf: crt0.o $(REBOOT_OBJS) $(C_OBJS)

$(TARGET): main.elf
	$(Q)$(STRIP) -s $<
	$(Q)$(OBJCOPY) -O binary $< $(TARGET)
	$(Q)bin2c $@ $(TARGET).h payloadex
	@echo GET $@

clean:
	$(Q)rm -rf *~ *.s *.o *.elf $(TARGET).bin $(TARGET).h $(EXTRA_CLEAN)

include $(ARKSDK)/build-tools/beauty_bin.mak
