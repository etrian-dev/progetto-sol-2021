CC = gcc -std=c99 -D_GNU_SOURCE=1
CFLAGS = -g -Wall -Wextra -pedantic
HEADERS = -I . -I .. -I ../fs-api

LIBPATH = -Wl,-rpath=./libs -L ../libs
LIBS = -lfs-api -lutilities

CLIENT = ../client.out

$(CLIENT): client.o client-utils.o
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -o $@ $^ $(LIBS)
	-mv $^ ../objs
client.o: client.c
	$(CC) $(CFLAGS) $(HEADERS) -c -o $@ $<
client-utils.o: client-utils.c
	$(CC) $(CFLAGS) $(HEADERS) -c -o $@ $<

