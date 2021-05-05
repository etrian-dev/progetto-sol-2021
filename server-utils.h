// file header del server che contiene la definizione di cose utili al server

#ifndef FS_SERVER_H_INCLUDED
#define FS_SERVER_H_INCLUDED

// struttura contenente i parametri del server
struct serv_params {
	unsigned int thread_pool;   // numero di thread worker del server
	unsigned int max_memsz;     // massima occupazione memoria dei file
	unsigned int max_fcount;    // massimo numero di file gestiti contemporaneamente
	char *sock_basepath;        // path base del socket server
	char *log_basepath;         // path base del file di log
}

// definisco il path del file di configurazione come macro
#define CONF_PATH "./config.txt"

// funzione per il parsing del file di configurazione
int parse_config(struct serv_params *params);

// definisco una dimensione base dei buffer
#define BUF_BASESZ 100

// funzione di riallocazione del buffer a newsz.
// Ritorna 0 se ha successo, -1 se fallisce
int rialloca_buffer(char **buf, size_t newsz);

#endif
