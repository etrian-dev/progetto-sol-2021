#Makefile per la compilazione del progetto

CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .

SERVER = fs-server.out
CLIENT = client.out

.PHONY: all clean cleanall fs-api

all: $(SERVER) fs-api

$(SERVER): parse_config.c server.c utils.c logging.c
	$(CC) $(CFLAGS) $(HEADERS) -o $@ $^

fs-api:
	cd fs-api && $(MAKE)
