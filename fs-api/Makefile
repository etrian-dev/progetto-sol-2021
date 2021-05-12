CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .

#la lib condivisa sarà messa nella directory libs del progetto
LIBFS_API = ../libs/libfs-api.so

$(LIBFS_API): fs-api.o
	$(CC) $(CFLAGS) -shared -o $@ $<
	mv $< ../objs/
fs-api.o: fs-api.c
	$(CC) $(CFLAGS) $(HEADERS) -c -fPIC -o $@ $<
