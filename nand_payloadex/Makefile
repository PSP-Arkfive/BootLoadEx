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
	lib.o \
	pspbtcnf.o \
	
C_OBJS = \
	$(BOOTLOADEX)/scanner.o \

LIBS = -L $(PSPSDK)/lib

ifdef DEBUG
CFLAGS += -DDEBUG=$(DEBUG)
LIBS += -lcolordebugger
endif

crt0.o : $(BOOTLOADEX)/crt0.S
	$(CC) -I$(PSPSDK)/include -c $< -o crt0.o

$(REBOOT_OBJS) : %.o : $(BOOTLOADEX)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

patches.o: $(BOOTLOADEX)/psp_rebootex/patches.c
	$(CC) $(CFLAGS) -I$(BOOTLOADEX) -c $< -o $@

main.elf: crt0.o $(REBOOT_OBJS) $(C_OBJS) patches.o

$(TARGET): main.elf
	$(Q)$(STRIP) -s $<
	$(Q)$(OBJCOPY) -O binary $< $(TARGET)
	$(Q)bin2c $@ nand_payloadex.h payloadex
	@echo GET $@

clean:
	$(Q)rm -rf *~ *.s *.o *.elf nand_payloadex.bin nand_payloadex.h ms_payloadex.h $(EXTRA_CLEAN)

include $(ARKSDK)/build-tools/beauty_bin.mak
