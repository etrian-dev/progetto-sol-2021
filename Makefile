#Makefile per la compilazione del progetto

CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .

CLIENT = client.out

.PHONY: all clean cleanall server fs-api

server:
	cd fs-server && $(MAKE)

$(CLIENT): client.c client-utils.c utils.c
	$(CC) $(CFLAGS) $(HEADERS) -o $@ $^

fs-api:
	cd fs-api && $(MAKE)
