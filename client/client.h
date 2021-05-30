// header del client, contente definizioni di funzione e macro utili al client
#ifndef CLIENT_H_INCLUDED
#define CLIENT_H_INCLUDED

#include <utils.h> // per il tipo Queue
#include <stddef.h>

// messaggio di help
#define HELP_MSG \
"Usage: %s [OPTIONS]\n\nOPTIONS\n\
\t-h: stampa questo messaggio e termina\n\
\t-f <filename>: specifica il socket AF_UNIX a cui il client prova a connettersi\n\
\t-r <file1> [, <filen>]: specifica una lista di file da leggere nel server\n\
\t-R [n=0]: specifica un numero massimo di file da leggere (se <= 0 prova a leggere tutti i file nel server\n\
\t-d <dirname>: specifica la directory in cui salvare eventuali file letti dal server\n\
\t-t <time >= 0>: delay tra le richeste al server (in msec)\n\
\t-p: stampa su stdout ogni operazione del client\n"

// stringa delle opzioni per getopt
#define CLIENT_OPSTRING ":hf:D:d:R:r:W:w:A:a:t:p"
// i due punti iniziali per distinguere tra opzione non riconosciuta e argomento mancante

// definisco una struttura che conterrà i valori delle opzioni specificate
struct client_opts {
    short int help_on;                // default: 0 (non stampa il messaggio di help)
    short int prints_on;              // default: 0 (non stampa le operazioni su stdout)
    char *fs_socket;                  // default: NULL
    long int rdelay;                  // default: 0 (interpretato come numero di millisecondi)

    long int nread;                   // default: 0 (interpretato come nessun limite)
    long int nwrite;                  // default: 0 (interpretato come nessun limite)

    char *dir_save_reads;             // default: NULL
    char *dir_write;                  // default: NULL
    char *dir_swapout;                // default: NULL

    struct Queue *oplist;             // default: NULL
};
// definisco una struttura che contiene l'operazione richiesta e la lista di file su cui effettuarla
struct operation {
    char type;
    struct Queue *flist;
};

// nargs è argc, args è argv del programma client: i parametri sono restituiti nella struttura
int get_client_options(int nargs, char **args, struct client_opts *params);

int process_filelist(struct Queue *ops, char *arg);

// Libera la struttura
void free_client_opt(struct client_opts *options);

#endif
