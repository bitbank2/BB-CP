PROCESSOR:=$(shell uname -m)
ifeq ($(PROCESSOR), armv6l)
# Must be Raspberry Pi Zero
CFLAGS=-c -I/opt/vc/include -Wall -O3 -D_RPIZERO_
LIBS= -lspi_lcd -lpigpio -L/opt/vc/lib -lbcm_host -lpthread -lm
else
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

