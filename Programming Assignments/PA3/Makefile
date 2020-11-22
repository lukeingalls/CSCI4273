CC=gcc
EXE=webproxy
CFLAGS=-Wall -pthread -lcrypto
PORT=5001

all:
	$(CC) proxy.c $(CFLAGS) -o $(EXE)

webproxy:
	$(CC) proxy.c $(CFLAGS) -o $(EXE) && ./$(EXE) $(PORT) &
