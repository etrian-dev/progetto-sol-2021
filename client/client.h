// header del client, contente definizioni di funzione e macro utili al client
#ifndef CLIENT_H_INCLUDED
#define CLIENT_H_INCLUDED

#include <utils.h> // per il tipo struct Queue

// messaggio di help
#define HELP_MSG \
"Usage: %s [OPTIONS]\n\nOPTIONS\n\
\t-h: stampa questo messaggio e termina\n\
\t-f <filename>: specifica il socket AF_UNIX a cui il client prova a connettersi\n\
\t-r <file1> [, <filen>]: specifica una lista di file da leggere nel server\n\
\t-R [n=0]: specifica un numero massimo di file da leggere (se <= 0 prova a leggere tutti i file nel server\n\
\t-d <dirname>: specifica la directory in cui salvare eventuali file letti dal server\n\
\t-a dest:scr [, destN:srcN]: specifica il file su cui fare append e il file/la stringa da concatenare\n\
\t-t <time >= 0>: delay tra le richeste al server (in msec)\n\
\t-p: stampa su stdout ogni operazione del client\n"

// stringa delle opzioni per getopt
#define CLIENT_OPSTRING ":hf:pR:r:W:w:a:t:"
// i due punti iniziali per distinguere tra opzione non riconosciuta e argomento mancante

// definisco una struttura che conterrà i valori delle opzioni specificate
struct client_opts {
    short int help_on;                // default: 0 (non stampa il messaggio di help)
    short int prints_on;              // default: 0 (non stampa le operazioni su stdout)
    char *fs_socket;                  // default: NULL
    long int rdelay;                  // default: 0 (interpretato come numero di millisecondi)
    struct Queue *oplist;             // default: NULL
};
// inizializza i parametri a valori di default
struct client_opts *init_params(void);
// Libera la struttura
void free_client_opt(struct client_opts *options);

// definisco una struttura che contiene l'operazione richiesta e la lista di file su cui effettuarla
struct operation {
    char type;           // Il tipo dell'operazione: read, write, etc..
    struct Queue *flist; // La lista di file sui quali applicare l'operazione (dove rilevante)
    char *dirname;       // path della directory (settato se rilevante)
    long int max_read;   // numero massimo di file da leggere (usato solo per readN)
    long int max_write;  // numero massimo di file da scrivere (usato solo per -w)
};

// nargs è argc, args è argv del programma client: i parametri sono restituiti nella struttura
int get_client_options(int nargs, char **args, struct client_opts *params);
// Data la coda (inizializzata) ops e la stringa arg, concatena in coda ad ops una
// nuova operazione del tipo dato dal terzo parametro contenente la lista
// di file ottenuta da arg e ritorna 0 se ha successo, -1 altrimenti
int process_filelist(struct Queue *ops, char *arg, char op_type);
// funzione per visitare ricorsivamente a partire da basedir al più nleft files
// I path dei file trovati sono inseriti nella coda pathlist ed il loro numero contenuto
// nella coda datalist è ritornato (o -1 per errore)
long int visit_dir(struct Queue *pathlist, struct Queue *datalist, const char *basedir, long int nleft);

#endif
