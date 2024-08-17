TARGET = eboot

OBJS = main.o #common.o

EXTRA_TARGETS = EBOOT.PBP

PSP_EBOOT_PSAR = data.zip

PSP_EBOOT_TITLE = Krazynez PSP Toolkit

PSP_EBOOT_ICON = ../res/ICON0.PNG

PSP_EBOOT_PIC1 = ../res/PIC1.PNG



CFLAGS = -std=c99



BUILD_PRX = 1

LIBS = -lz -lpspctrl


PSPSDK = $(shell psp-config -p)
include $(PSPSDK)/lib/build.mak


