CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I . -I ..

LIBPATH = -Wl,-rpath=./libs -L ../libs
LIBS = -lutilities

#la lib condivisa sarà messa nella directory libs del progetto
LIBFS_API = ../libs/libfs-api.so

$(LIBFS_API): fs-api.o
	$(CC) $(CFLAGS) $(LIBPATH) -shared -o $@ $< $(LIBS)
	mv $< ../objs/
fs-api.o: fs-api.c
	$(CC) $(CFLAGS) $(HEADERS) -c -fPIC -o $@ $<
fs-api-utils.o: fs-api-utils.c
	$(CC) $(CFLAGS) $(HEADERS) -c -fPIC -o $@ $<
