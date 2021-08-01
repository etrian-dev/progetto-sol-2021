/**
 * \file utils.h
 * \brief Header funzioni di utilità
 *
 * Questo header contiene le dichiarazioni delle funzioni di utilità implementate in utils.c,
 * tra cui vi sono quelle per la gestione di una coda concorrente (struct Queue)
 */

// Header per funzioni di utilità sia per i client che per il server
#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include <stddef.h>
#include <time.h>

/// definisco una dimensione base dei buffer
#define BUF_BASESZ 512

/// Permessi di lettura/scrittura per l'utente e lettura per tutti gli altri
#define PERMS_ALL_READ S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH

/// Funzione di riallocazione del buffer a newsz.
int rialloca_buffer(char **buf, size_t newsz);
/// Funzione per ottenere un path con la seguente struttura: "base/name"
char *get_fullpath(const char *base, const char *name);
/// Funzione per convertire una stringa s in un long int con error checking
int isNumber(const char* s, long* n);
/// Funzione per la duplicazione di una stringa allocata sullo heap
int string_dup(char **dest, const char *src);
/// Funzione di utilità per convertire msec in struct timespec
void get_delay(const int msec, struct timespec *delay);

//-----------------------------------------------------------------------------------
// Una coda generica

/**
 * \brief Nodo di una struct Queue
 */
struct node_t {
    void *data;            ///< I dati contenuti nel nodo
    size_t data_sz;        ///< La dimensione del campo data
    int socket;            ///< Il socket associato ai dati
    struct node_t *next;   ///< Puntatore al prossimo nodo della lista concatenata
};
/**
 * \brief Una coda generica realizzata con una lista concatenata semplice
 */
struct Queue {
    struct node_t *head; ///< Puntatore alla testa della lista
    struct node_t *tail; ///< Puntatore alla coda della lista
};

/// Inizializza una coda (vuota)
struct Queue *queue_init(void);
/// Libera la coda puntata da q (e tutti i suoi nodi)
void free_Queue(struct Queue *q);
/// Inserisce il buffer data (di size bytes) nella coda
int enqueue(struct Queue *q, const void *data_ptr, size_t size, const int csock);
/// Rimuove e ritorna l'elemento di testa della coda o ritorna NULL se la coda è vuota
struct node_t *pop(struct Queue *q);

//-----------------------------------------------------------------------------------
// Funzioni per la scrittura su file descriptor di esattamente n bytes

/// Legge esattamente n bytes dal fd in ptr
int readn(int fd, void *ptr, size_t n);
/// Scrive esattamente n bytes di ptr in fd
int writen(int fd, void *ptr, size_t n);

#endif
