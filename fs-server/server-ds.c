// header progetto
#include <utils.h>
#include <server-utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

// File contenente la gestione delle strutture dati del server

// Dichiaro qui tutte le strutture dati condivise

// hash table condivisa nel server
icl_hash_t *fs_table;
// mutex per l'accesso alla ht
pthread_mutex_t mux_ht;

// coda per la comunicazione con i worker
struct Queue *job_queue;
// lock e cond variables per l'accesso in ME
pthread_mutex_t mux_jobq;
pthread_cond_t new_job;

// coda per l'implementazione della politica di rimpiazzamento FIFO
struct Queue *cache_q;
// lock e cond variables per l'accesso in ME
pthread_mutex_t mux_cacheq;
pthread_cond_t new_cacheq;

int init_ds(struct serv_params *params) {
    // creo la ht per la memorizzazione dei file
    // Alloco preventivamente slot per il massimo numero di file che possono essere
    // inseriti nel server. La funzione hash è quella fornita da icl_hash e la chiave
    // sarà il path, per cui string_compare va bene per confrontare le chiavi
    if((fs_table = icl_hash_create(params->max_fcount, hash_pjw, string_compare)) == NULL) {
	// errore nella creazione della ht (probabilmente malloc)
	return -1;
    }
}
