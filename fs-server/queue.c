#include <queue.h>

#include <pthread.h>

#include <stddef.h>
#include <string.h>
#include <stdlib.h>

// file contenente l'implementazione di una coda accedibile in mutua esclusione

pthread_mutex_t mux;
pthread_cond_t new_request;

int init_queue(Queue **q) {
  if(!q) {
    return -1;
  }

  // inizializzo la lock
  pthread_mutex_init(&mux, NULL); // ha sempre successo
  if(pthread_cond_init(&new_request, NULL) == -1) {
    return -1;
  }

  // alloco una coda sincronizzata
  *q = malloc(sizeof(struct Queue));
  if(!q) {
    // errore di allocazione
    return -1;
  }
  (*q)->head = (*q)->tail = NULL;
  return 0;
}

// funzione per aggiungere alla coda un elemento
int enqueue(struct node_t **head, struct node_t **tail, const void *data, size_t size) {
  // accesso in mutua esclusione alla coda
  if(pthread_mutex_lock(&mux) != 0) {
    // errore nell'acquisizione della lock sulla coda
    return -1;
  }

	struct node_t *elem = malloc(sizeof(struct node_t));
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

struct node_t *pop(struct node_t **head, struct node_t **tail) {
  // accesso in mutua esclusione alla coda
  if(pthread_mutex_lock(&mux) != 0) {
    // errore nell'acquisizione della lock sulla coda
    return NULL;
  }

	if(*head) {
		struct node_t *tmp = *head;
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
