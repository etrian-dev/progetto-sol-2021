// file header del server che contiene la definizione di cose utili al server

#ifndef FS_SERVER_H_INCLUDED
#define FS_SERVER_H_INCLUDED

#include <stddef.h> // per il tipo size_t
#include <icl_hash.h> // per hashtable
#include <utils.h> // per la coda sincronizzata

//-----------------------------------------------------------------------------------
// Parsing del file di config

// definisco il path di default del file di configurazione come macro
#define CONF_PATH_DFL "./config.txt"
// definisco i permessi del file di log se devo crearlo
#define ALL_READ S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH

// struttura contenente i parametri del server
struct serv_params {
    long int thread_pool;   // numero di thread worker del server
    long int max_memsz;     // massima occupazione memoria dei file
    long int max_fcount;    // massimo numero di file gestiti contemporaneamente
    char *sock_path;        // path del socket server
    char *log_path;         // path del file di log
};

// i field del file di configurazione sono stringhe definite a tempo di compilazione
#define TPOOLSIZE "tpool"
#define MAXMEM "maxmem"
#define MAXFILES "maxfiles"
#define SOCK_PATH "sock_basepath"
#define LOG_PATH "log_basepath"
// valori di default per i parametri
#define TPOOL_DFL 10
#define MAXMEM_DFL 1024
#define MAXFILES_DFL 10
#define SOCK_PATH_DFL "./fsserver.sock"
#define LOG_PATH_DFL "./fsserver.log"
// definisco anche un limite superiore (ragionevole) alla lunghezza dei path di default
#define DFL_PATHLEN 50

// funzione per il parsing del file di configurazione
int parse_config(struct serv_params *params, const char *conf_fpath);


//-----------------------------------------------------------------------------------
// Strutture dati del server per la memorizzazione dei file e gestione delle richieste della API

// Struttura dati che contiene i dati e metadati di un file nel fileserver
struct fs_filedata_t {
    void *data;
    size_t size;
    int *openedBy;
    size_t nopened;
    int lockedBy;
};

// hash table condivisa nel server
extern icl_hash_t *fs_table;
// mutex per l'accesso alla ht
extern pthread_mutex_t mux_ht;

// coda per la comunicazione con i worker
extern struct Queue *job_queue;
// lock e cond variables per l'accesso in ME
extern pthread_mutex_t mux_jobq;
extern pthread_cond_t new_job;

// coda per l'implementazione della politica di rimpiazzamento FIFO
extern struct Queue *cache_q;
// lock e cond variables per l'accesso in ME
extern pthread_mutex_t mux_cacheq;
extern pthread_cond_t new_cacheq;

// Funzione che inizializza tutte le strutture dati: prende in input i parametri del server
int init_ds(struct serv_params *params);

//-----------------------------------------------------------------------------------
//Gestione dei log

// Funzione per effettuare il logging: prende come parametri
// 1) il file descriptor (deve essere già aperto in scrittura) del file di log
// 2) il codice di errore (errno)
// 3) la stringa da stampare nel file (null-terminated)
// La funzione ritorna:
// 0 se ha successo
// -1 se riscontra un errore (settato errno)
int log(int log_fd, int errcode, char *message);

// Funzione eseguita dal worker
void *work(void *queue);

#endif
