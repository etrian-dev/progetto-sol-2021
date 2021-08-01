/**
 * \file utils.c
 * \brief File contenente l'implementazione di funzioni di utilità
 *
 * Questo file contiene l'implementazione delle funzioni di utilità definite in utils.h
 * tra cui vi sono quelle per la gestione di una coda concorrente (struct Queue)
 */


// header utilità
#include <utils.h>
// system call headers
#include <sys/types.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <time.h>

/**
 * Dato il buffer buf, allocato sullo heap e di dimensione qualunque,
 * lo rialloca alla dimensione newsz
 * \param [in,out] buf Puntatore doppio al buffer da riallocare
 * \param [in] newsz La nuova dimensione del buffer dopo l'esecuzione
 * \return Ritorna 0 se ha successo (ed eventualmente modifica il contenuto del puntatore buf), -1 altrimenti
 */
int rialloca_buffer(char **buf, size_t newsz) {
    char *newbuf = realloc(*buf, newsz);
    if(!newbuf) {
        // errore di allocazione
        return -1;
    }
    *buf = newbuf;
    return 0;
}

/**
 * Partendo dalla stringa base e name construisce il path base/name (duplica gli argomenti)
 * \param [in] base La base del path da costruire
 * \param [in] name La parte finale del path da costruire (può contenere '/')
 * \return Ritorna un puntatore alla stringa "base/name" se ha successo, NULL altrimenti
 */
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

/**
 * Funzione per convertire una stringa s in un long int.
 * isNumber ritorna:
 * - 0: conversione ok
 * - 1: non e' un numbero
 * - 2: overflow/underflow
 * \param [in] s La stringa da convertire in un intero
 * \param [out] n Puntatore all'intero risultato della conversione
 * \return Ritorna un numero corrispondente all'esito della conversione, come riportato nella descrizione
 */
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

int string_dup(char **dest, const char *src) {
    if((*dest = strndup(src, strlen(src) + 1)) == NULL) {
        // errore di duplicazione della stringa, riporto il codice di errore al chiamante
        return -1;
    }
    return 0;
}

/**
 * La procedura converte un certo tempo espresso in millisecondi (msec) nella struttura
 * struct timespec, che ha campi tv_sec (numero di secondi) e tv_nsec (numero di nanosecondi).
 * La conversione è effettuata in modo da prevenire eventuali overflow del campo nsec se msec > 1000
 * \param [in] msec Il numero di millisecondi da convertire
 * \param [out] delay La struttura risultato della conversione (passata come puntatore per far persistere la modifica nel chiamante)
 */
void get_delay(const int msec, struct timespec *delay) {
    // dato che il delay è specificato in ms devo convertirlo a ns per scriverlo in timespec
    // Se il delay in ms è troppo grande (>=1000) per essere convertito in ns provo a convertire in secondi
    // Se non gestissi questi due casi potrei avere overflow su tv_nsec
    if(msec >= 1000) {
        delay->tv_sec = (time_t)(msec / 1000); // con la divisione intera ottengo il numero di secondi in msec ms
        delay->tv_nsec = (msec % 1000) * 1000000; // il resto è convertito a ns e non posso avere overflow
    }
    else {
        delay->tv_sec = 0;
        delay->tv_nsec = msec * 1000000;
    }
}

/**
 * Inizializza una coda struct Queue, inizialmente vuota
 * \return Ritorna un puntatore alla coda allocata dinamicamente se ha successo, NULL altrimenti
 */
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

/**
 * Libera una coda struct Queue, effettuando free anche sui suoi nodi
 * \param [in] q Un puntatore alla coda da liberare
 */
void free_Queue(struct Queue *q) {
    struct node_t *a = NULL;
    while((a = pop(q)) != NULL) {
        if(a->data) free(a->data);
        free(a);
    }
    free(q);
}

/**
 * La funzione tenta di aggiungere in coda a q il nodo contenente i dati puntati da data_ptr, di
 * dimensione size. Può essere settato il campo socket del nodo, se utile.
 * NOTA: L'operazione richiede sincronizzazione esplicita a carico del chiamante se eseguita
 * potenzialmente da più thread in contemporanea
 * \param [in,out] q La coda in cui si vuole inserire il nodo
 * \param [in] data_ptr I dati da inserire nel nuovo nodo
 * \param [in] size La dimensione del buffer data_ptr
 * \param [in] csock Un descrittore di socket mantenuto dal nodo
 * (di solito usato per sapere a quale socket inviare la risposta di una struct request_t)
 * \return Ritorna 0 se l'operazione ha successo, -1 altrimenti
 */
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

/**
 * La funzione tenta di rimuovere il nodo di testa della coda q passata come parametro
 * \param [in,out] q La coda da cui si vuole estrarre un nodo
 * \return Ritorna un puntatore al nodo estratto da q se ha successo, NULL se la coda era vuota o non inizializzata
 */
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
        errno = 0; // reset errno to handle EINTR internally
        if((nread = read(fd, ptr, nleft)) < 0) {
            if(errno == EINTR) {
                continue; // write was interrupted by a signal: retry
            }
            else if (nleft == n) return -1; /* error, return -1 */
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
        errno = 0; // reset errno to handle EINTR internally
        if((nwritten = write(fd, ptr, nleft)) < 0) {
            if(errno == EINTR) {
                continue; // write was interrupted by a signal: retry
            }
            else if (nleft == n) return -1; /* error, return -1 */
            else break; /* error, return amount written so far */
        } else if (nwritten == 0) break;
        nleft -= nwritten; /* EOF */
        ptr += nwritten;
    }
    return(n - nleft); /* return >= 0 */
}
