CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .

#la lib condivisa sarà messa nella directory libs del progetto
LIBFS_API = libfs-api.so

.PHONY: clean cleanall

$(LIBFS_API): fs-api.o
	mkdir -p ../libs
	$(CC) $(CFLAGS) $(HEADERS) -shared -o ../libs/$@ $<
fs-api.o: fs-api.c
	$(CC) $(CFLAGS) $(HEADERS) -c -fPIC -o $@ $<
clean: $(LIBFS_API)
	-rm -fr $(LIBFS_API)
cleanall: clean $(wildcard *.o)
	-rm -fr $(wildcard *.o)
