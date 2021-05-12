CC = gcc
CFLAGS = -g -Wall -pedantic -std=c99
HEADERS = -I .

#la lib condivisa sarà messa nella directory libs del progetto
LIBFS_API = libfs-api.so

.PHONY: clean cleanall

%.o: %.c
    $(CC) $(CFLAGS) -c -fPIC -o $@ $<

$(LIBFS_API): fs-api.o
    mkdir -p ../libs
    $(CC) $(CFLAGS) -shared -o ../libs/$@ $<
clean: $(LIBFS_API)
    -rm -fr $(LIBFS_API)
cleanall: clean $(wildcard *.o)
    -rm -fr $(wildcard *.o)
