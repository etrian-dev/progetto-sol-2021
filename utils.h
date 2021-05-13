// Header per funzioni di utilità sia per i client che per il server
#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include <pthread.h>
#include <stddef.h>

// definisco una dimensione base dei buffer
#define BUF_BASESZ 100

// funzione di riallocazione del buffer a newsz.
// Ritorna 0 se ha successo, -1 se fallisce
int rialloca_buffer(char **buf, size_t newsz);
// identica alla precedente, ma prende in ingresso un array di stringhe da ridimensionare
//int rialloca_arr(char ***arr, size_t newlen);
// Funzione per convertire una stringa s in un long int
int isNumber(const char* s, long* n);
// Funzione per la duplicazione di una stringa allocata sullo heap
int string_dup(char **dest, const char *src);

// definisco un mutex per l'accesso alla coda
extern pthread_mutex_t mux;
// definisco operazioni sulla coda concorrente
// defines the Queue structure: the data field holds a string
// and the next field a pointer to the next element in the queue
struct Queue {
    void *data_ptr;
    struct Queue *next;
};

// Inserisce data (di size bytes) nella coda; Se fallisce ritorna -1, altrimenti 0
int enqueue(struct Queue **head, struct Queue **tail, const void *data, size_t size);

// Rimuove l'elemento alla testa della coda o ritorna NULL se la coda è vuota
struct Queue *pop(struct Queue **head, struct Queue **tail);

// Legge esattamente n bytes dal fd
ssize_t readn(int fd, void *ptr, size_t n);
// Scrive esattamente n bytes nel fd
ssize_t writen(int fd, void *ptr, size_t n);


#endif
