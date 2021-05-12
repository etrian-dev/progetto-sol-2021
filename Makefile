#Makefile per la compilazione del progetto

CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .


CLIENT = client.out

.PHONY: all clean cleanall server

server:
	cd fs-server && $(MAKE)



$(CLIENT): client.c client-utils.c utils.c
	$(CC) $(CFLAGS) $(HEADERS) -o $@ $^
