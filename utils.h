// Header per funzioni di utilità sia per i client che per il server
#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include <stddef.h>

// definisco una dimensione base dei buffer
#define BUF_BASESZ 512

// Permessi di lettura/scrittura per l'utente e lettura per tutti gli altri
#define PERMS_ALL_READ S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH

// funzione di riallocazione del buffer a newsz.
// Ritorna 0 se ha successo, -1 se fallisce
int rialloca_buffer(char **buf, size_t newsz);
char *get_fullpath(const char *base, const char *name);
// Funzione per convertire una stringa s in un long int
int isNumber(const char* s, long* n);
// Funzione per la duplicazione di una stringa allocata sullo heap
// Ritorna 0 se ha successo, -1 altrimenti
int string_dup(char **dest, const char *src);

// Definisco una coda come lista concatenata
struct node_t {
    void *data;            // i dati contenuti nella coda
    size_t data_sz;        // la dimensione del buffer dati
    int socket;            // il socket associato ai dati
    struct node_t *next;   // puntatore al prossimo nodo della lista concatenata
};
struct Queue {
    struct node_t *head;
    struct node_t *tail;
};

// inizializza una coda (vuota): ritorna un puntatore ad essa se ha successo, NULL altrimenti
struct Queue *queue_init(void);
// Libera la coda puntata da q
void free_Queue(struct Queue *q);

// Inserisce data (di size bytes) nella coda; Se fallisce ritorna -1, altrimenti 0
int enqueue(struct Queue *q, const void *data_ptr, size_t size, const int csock);

// Rimuove l'elemento alla testa della coda o ritorna NULL se la coda è vuota
struct node_t *pop(struct Queue *q);

// Legge esattamente n bytes dal fd
int readn(int fd, void *ptr, size_t n);
// Scrive esattamente n bytes nel fd
int writen(int fd, void *ptr, size_t n);

#endif
