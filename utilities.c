// header progetto
#include <server-utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

// sorgente contenente varie funzioni di utilità

// Questa funzione prova a riallocare un buffer ad una nuova dimensione passata come parametro
// Ritorna 0 se ha successo, -1 se si sono verificati errori
int rialloca_buffer(char **buf, size_t newsz) {
	char *newbuf = realloc(*buf, newsz);
	if(!newbuf) {
		// errore di allocazione
	}
	*buf = newbuf;
	return 0;
}
