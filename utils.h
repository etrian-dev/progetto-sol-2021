// Header per funzioni di utilità sia per i client che per il server
#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include <stddef.h>

// definisco una dimensione base dei buffer
#define BUF_BASESZ 100

// funzione di riallocazione del buffer a newsz.
// Ritorna 0 se ha successo, -1 se fallisce
int rialloca_buffer(char **buf, size_t newsz);
// identica alla precedente, ma prende in ingresso un array di stringhe da ridimensionare
int rialloca_arr(char ***arr, size_t newlen);
// Funzione per convertire una stringa s in un long int
int isNumber(const char* s, long* n);
// Funzione per la duplicazione di una stringa allocata sullo heap
int string_dup(char **dest, const char *src);

#endif
