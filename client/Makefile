CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I . -I ..

LIBPATH = -Wl,-rpath=./libs -L ../libs
LIBS = -lutilities

CLIENT = ../client.out

$(CLIENT): client.o client-utils.o
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -o $@ $^ $(LIBS)
	mv $^ ../objs
client.o: client.c client.h
	$(CC) $(CFLAGS) $(HEADERS) -c -o $@ $< -lutilities
client-utils.o: client-utils.c client.h
	$(CC) $(CFLAGS) $(HEADERS) -c -o $@ $< -lutilities

