// file header del server che contiene la definizione di cose utili al server

#ifndef FS_SERVER_H_INCLUDED
#define FS_SERVER_H_INCLUDED

#include <stddef.h> // per il tipo size_t

// definisco il path del file di configurazione come macro
#define CONF_PATH "./config.txt"

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
int parse_config(struct serv_params *params);

// definisco una dimensione base dei buffer
#define BUF_BASESZ 100

// funzione di riallocazione del buffer a newsz.
// Ritorna 0 se ha successo, -1 se fallisce
int rialloca_buffer(char **buf, size_t newsz);
// Funzione per convertire una stringa s in un long int
int isNumber(const char* s, long* n);

#endif
