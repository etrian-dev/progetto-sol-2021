// header progetto
#include <utils.h>
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

// Funzione per convertire una stringa s in un long int
// isNumber ritorna
//	0: ok
//	1: non e' un numbero
//	2: overflow/underflow
//
int isNumber(const char* s, long* n) {
  if (s==NULL) return 1;
  if (strlen(s)==0) return 1;
  char* e = NULL;
  errno=0;
  long val = strtol(s, &e, 10);
  if (errno == ERANGE) return 2;    // overflow
  if (e != NULL && *e == (char)0) {
    *n = val;
    return 0;   // successo
  }
  return 1;   // non e' un numero
}

// duplico la stringa
int string_dup(char *dest, const char *src) {
    if((dest = strndup(src, strlen(src) + 1) == NULL) {
	// errore di duplicazione della stringa, riporto il codice di errore al chiamante
	return -1;
    }
    return 0;
}
