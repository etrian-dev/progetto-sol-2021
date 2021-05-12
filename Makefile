#Makefile per la compilazione del progetto

CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .

SERVER = fs-server.out
CLIENT = client.out

.PHONY: all clean cleanall

$(SERVER): parse_config.c server.c utils.c logging.c
	$(CC) $(CFLAGS) $(HEADERS) -o $@ $^




$(CLIENT): client.c client-utils.c utils.c
	$(CC) $(CFLAGS) $(HEADERS) -o $@ $^
