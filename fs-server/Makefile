CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .. -I .

SERVER = fs-server.out

$(SERVER): parse_config.c server.c utils.c logging.c worker.c
	$(CC) $(CFLAGS) $(HEADERS) -o ../$@ $^
