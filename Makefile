#Makefile per la compilazione del progetto

CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .

SERVER = fs-server.out
CLIENT = client.out

.PHONY: all clean cleanall

$(SERVER): parse_config.c server.c utilities.c
	$(CC) $(CFLAGS) $(HEADERS) -o $@ $^
