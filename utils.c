// header utilità
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

char *get_fullpath(const char *base, const char *name) {
    size_t path_sz = strlen(base) + strlen(name) + 2;
    char *full_path = calloc(path_sz, sizeof(char));
    if(!full_path) {
        return NULL;
    }
    strncat(full_path, base, path_sz);
    strncat(full_path, "/", 2);
    strncat(full_path, name, path_sz);
    return full_path;
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
int string_dup(char **dest, const char *src) {
    if((*dest = strndup(src, strlen(src) + 1)) == NULL) {
        // errore di duplicazione della stringa, riporto il codice di errore al chiamante
        return -1;
    }
    return 0;
}

// inizializza una coda (vuota): ritorna un puntatore ad essa se ha successo, NULL altrimenti
struct Queue *queue_init(void) {
    struct Queue *q = malloc(sizeof(struct Queue));
    if(!q) {
        return NULL;
    }
    // La coda inizialmente è vuota!
    q->head = NULL;
    q->tail = NULL;
    return q;
}

// Libera la coda puntata da q
void free_Queue(struct Queue *q) {
    struct node_t *a = NULL;
    while((a = pop(q)) != NULL) {
        free(a->data);
        free(a);
    }
    free(q->head);
    free(q->tail);
    free(q);
}

// funzione per aggiungere alla coda un elemento
int enqueue(struct Queue *q, const void *data_ptr, size_t size, const int csock) {
    // alloco un nuovo nodo della coda
    struct node_t *elem = malloc(sizeof(struct node_t));
    if(!elem) {
        // errore di allocazione
        return -1;
    }
    // alloco un'area di memoria per il dato da copiare
    if((elem->data = malloc(size)) == NULL) {
        // errore di allocazione: libero il nodo prima di uscire
        free(elem);
        return -1;
    }
    // copio i dati
    memcpy(elem->data, data_ptr, size);
    elem->data_sz = size; // setto la dimensione allocata, per usarla laddove rilevante
    elem->socket = csock;
    elem->next = NULL;

    // Il nuovo nodo è aggiunto in fondo alla coda
    if(q->tail) {
        q->tail->next = elem;
        q->tail = elem;
    }
    else {
        q->head = q->tail = elem;
    }
    return 0;
}

struct node_t *pop(struct Queue *q) {
    if(q && q->head) {
        struct node_t *tmp = q->head;
        if(q->head == q->tail) {
            q->tail = NULL;
        }
        q->head = q->head->next;
        // ritorno il nodo se ho successo, ma è responsabilità del chiamante deallocarlo
        return tmp;
    }
    // coda vuota o non inizializzata
    return NULL;
}

int readn(int fd, void *ptr, size_t n) {
    size_t nleft;
    int nread;
    nleft = n;
    while (nleft > 0) {
        if((nread = read(fd, ptr, nleft)) < 0) {
            if (nleft == n) return -1; /* error, return -1 */
            else break; /* error, return amount read so far */
        } else if (nread == 0) break; /* EOF */
        nleft -= nread;
        ptr += nread;
    }
    return(n - nleft); /* return >= 0 */
}

int writen(int fd, void *ptr, size_t n) {
    size_t nleft;
    int nwritten;
    nleft = n;
    while (nleft > 0) {
        if((nwritten = write(fd, ptr, nleft)) < 0) {
            if (nleft == n) return -1; /* error, return -1 */
            else break; /* error, return amount written so far */
        } else if (nwritten == 0) break;
        nleft -= nwritten;
        ptr += nwritten;
    }
    return(n - nleft); /* return >= 0 */
}
