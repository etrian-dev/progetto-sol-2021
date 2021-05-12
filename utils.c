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
#include <stddef.h>

// sorgente contenente varie funzioni di utilità

pthread_mutex_t mux = PTHREAD_MUTEX_INITIALIZER;

// Questa funzione prova a riallocare un buffer ad una nuova dimensione passata come parametro
// Ritorna 0 se ha successo, -1 se si sono verificati errori
int rialloca_buffer(char **buf, size_t newsz) {
  char *newbuf = realloc(*buf, newsz);
  if(!newbuf) {
    // errore di allocazione
    return -1;
  }
  *buf = newbuf;
  return 0;
}
// identica alla precedente, ma prende in ingresso un array di stringhe da ridimensionare
/*
int rialloca_arr(char ***arr, size_t newlen) {
  char **newarr = realloc(*arr, newlen);
>>>>>>> client
  if(!newarr) {
    return -1;
  }
  *arr = newarr;
  return 0;
}*/

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
int string_dup(char **dest, const char *src) {
  if((*dest = strndup(src, strlen(src) + 1)) == NULL) {
    // errore di duplicazione della stringa, riporto il codice di errore al chiamante
    return -1;
  }
  return 0;
}

// funzione per aggiungere alla coda un elemento
int enqueue(struct Queue **head, struct Queue **tail, const void *data, size_t size) {
  // accesso in mutua esclusione alla coda
  if(pthread_mutex_lock(&mux) != 0) {
    // errore nell'acquisizione della lock sulla coda
    return -1;
  }

	struct Queue *elem = malloc(sizeof(struct Queue));
	if(!elem) {
    // errore di allocazione
    // rilascio la mutua esclusione prima di uscire
    if(pthread_mutex_unlock(&mux) != 0) {
      // errore nel rilascio della lock sulla coda
      return -1;
    }
		return -1;
	}
	memcpy(elem->data_ptr, data, size);
	elem->next = NULL;
	if(*tail) {
		(*tail)->next = elem;
		*tail = elem;
	}
	else {
		*head = elem;
		*tail = elem;
	}

  // rilascio la mutua esclusione prima di uscire
  if(pthread_mutex_unlock(&mux) != 0) {
    // errore nel rilascio della lock sulla coda
    return -1;
  }
  return 0;
}

struct Queue *pop(struct Queue **head, struct Queue **tail) {
  // accesso in mutua esclusione alla coda
  if(pthread_mutex_lock(&mux) != 0) {
    // errore nell'acquisizione della lock sulla coda
    return NULL;
  }

	if(*head) {
		struct Queue *tmp = *head;
		if(*head == *tail) {
			*tail = NULL;
		}
		*head = (*head)->next;

    // rilascio la mutua esclusione prima di uscire
    if(pthread_mutex_unlock(&mux) != 0) {
      // errore nel rilascio della lock sulla coda
      return NULL;
    }

		return tmp; // returns the struct: needs to be freed by the caller
	}

  // rilascio la mutua esclusione prima di uscire
  if(pthread_mutex_unlock(&mux) != 0) {
    // errore nel rilascio della lock sulla coda
    return NULL;
  }

	// empty queue
	return NULL;
}
