TARGET = eboot

OBJS = main.o common.o crt0.o 

EXTRA_TARGETS = EBOOT.PBP

PSP_EBOOT_PSAR = data.zip

PSP_EBOOT_TITLE = Krazynez PSP Toolkit

PSP_EBOOT_ICON = ../res/ICON0.PNG

PSP_EBOOT_PIC1 = ../res/PIC1.PNG

INCDIR += ./inc
INCDIR += ../data/PRXs

CFLAGS = -std=c99 -Os -G0
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS) -c

#LDFLAGS = -mno-crt0 -nostartfiles
LDFLAGS += -nostdlib


BUILD_PRX = 1

PSP_FW_VERSION = 271

LIBDIR += ./libs
LIBS = -lz -lpspctrl -lvlfgui -lvlfgu -lvlfutils -lvlflibc -lpsprtc -lpspkubridge


PSPSDK = $(shell psp-config -p)
include $(PSPSDK)/lib/build.mak


