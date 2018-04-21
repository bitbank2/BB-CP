PROCESSOR:=$(shell grep -o "BCM.*" /proc/cpuinfo)
ifeq ($(PROCESSOR), BCM2708)
# Must be Raspberry Pi Zero
$(info Building for Raspberry Pi Zero)
CFLAGS=-c -I/opt/vc/include -Wall -O3 -D_RPIZERO_
LIBS= -lspi_lcd -lpigpio -L/opt/vc/lib -lbcm_host -lpthread -lm
else ifeq ($(PROCESSOR), BCM2835)
# Must be Raspberry Pi ZeroW
$(info Building for Raspberry Pi ZeroW)
CFLAGS=-c -I/opt/vc/include -Wall -O3 -D_RPIZERO_
LIBS= -lspi_lcd -lpigpio -L/opt/vc/lib -lbcm_host -lpthread -lm
else ifeq ($(PROCESSOR), BCM2837)
$(info Building for Raspberry Pi 3)
CFLAGS=-c -I/opt/vc/include -Wall -O3 -D_RPI3_
LIBS= -lspi_lcd -lpigpio -L/opt/vc/lib -lbcm_host -lpthread -lm
else
# All other boards
$(info Building for non-RPI board)
CFLAGS=-c -Wall -O3
LIBS= -lspi_lcd -lpthread -lm
endif

all: bbcp

bbcp: Makefile main.o
	$(CC) main.o $(LIBS) -o bbcp

main.o: main.c
	$(CC) $(CFLAGS) main.c

clean:
	rm *.o bbcp

