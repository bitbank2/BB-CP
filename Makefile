CFLAGS=-c -Wall -O3
#LIBS= -lspi_lcd -lpthread -lm
LIBS= -lspi_lcd -lpigpio -lpthread -lm
#LIBS= -lspi_lcd -lwiringPi -lm -lpthread
#LIBS= -lspi_lcd -lbcm2835 -lm

all: bbcp

bbcp: Makefile main.o
	$(CC) main.o $(LIBS) -o bbcp

main.o: main.c
	$(CC) $(CFLAGS) main.c

clean:
	rm *.o bbcp

