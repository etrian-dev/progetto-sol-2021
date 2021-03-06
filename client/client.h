// header del client, contente definizioni di funzione e macro utili al client
#ifndef CLIENT_H_INCLUDED
#define CLIENT_H_INCLUDED

#include <utils.h> // per il tipo struct Queue

// messaggio di help
#define HELP_MSG \
"Usage: %s [OPTIONS]\n\nOPTIONS\n\
\t-h: stampa questo messaggio e termina\n\
\t-p: stampa su stdout ogni operazione del client\n\
\t-t <time >= 0>: delay tra le richeste al server (espresso in millisecondi)\n\
\t-f <filename>: specifica il socket AF_UNIX a cui il client prova a connettersi\n\
\t-r <file1> [, <filen>]: specifica una lista di file da leggere nel server (almeno uno)\n\
\t-R [n=0]: specifica un numero massimo di file da leggere (se n == 0 legge tutti i file presenti nel server)\n\
\t-W <file1> [, <filen>]: specifica una lista di file da scrivere nel server (almeno uno)\n\
\t-w <dirname>[,n=0]: specifica una directory dalla quale leggere al piu' n files da\n\
\t\tscrivere nel fileserver (se possibile). Se n == 0 (opzionale: di default viene assunto n == 0)\n\
\t\tallora non sono posti limiti al numero di file da scrivere. La ricerca prosegue ricorsivamente\n\
\t\tvisitando le sottodirectory di dirname\n\
\t-d <dirname>: specifica la directory in cui salvare eventuali file letti dal server\n\
\t-D <dirname>: specifica la directory in cui salvare eventuali file espulsi dal server\n\
\t-a dest:src [, destN:srcN]: specifica una lista di coppie composte dal file su cui\n\
\t\teffettuare la richiesta di append (dest) e il file/la stringa da concatenare (src).\n\
\t\tLa semantica è di interpretare src come stringa se l'apertura di src come file fallisce\n"

// stringa delle opzioni per getopt
#define CLIENT_OPSTRING ":hf:R:r:W:w:l:u:c:a:t:d:D:p"
// i due punti iniziali per distinguere tra opzione non riconosciuta e argomento mancante

// definisco una struttura che conterrà i valori delle opzioni specificate
struct client_opts {
    short int help_on;                // default: 0 (non stampa il messaggio di help)
    short int prints_on;              // default: 0 (non stampa le operazioni su stdout)
    char *fs_socket;                  // default: NULL
    long int rdelay;                  // default: 0 (interpretato come numero di millisecondi)
    // coda di operazioni richieste dal client tramite gli argromenti da riga di comando
    struct Queue *oplist;             // default: NULL
};
// definisco una struttura che contiene l'operazione richiesta e la lista di file su cui effettuarla
struct operation {
    char type;                        // tipo di operazione richiesta
    long int max_n;                   // numero di file da leggere/scrivere massimo (settato se rilevante)
    struct Queue *flist;              // lista di file ai quali applicare l'operazione
    char *dir_op;                     // directory su cui operare (per l'opzione -w)
    char *dir_swp;                    // directory in cui salvare i file letti/espulsi (opzioni -d e -D)
};

// Alloca ed inizializza i parametri di una nuova operazione, ritornando un puntatore ad essa
struct operation *newop(const char t, const long int max, struct Queue *list, char *dir, char *swp);
// libera l'operazione op
void free_op(struct operation **op);

// alloca ed inizializza la struttura contenente le opzioni del client
struct client_opts *init_params(void);
// nargs è argc, args è argv del programma client: i parametri sono restituiti nella struttura
int get_client_options(int nargs, char **args, struct client_opts *params);
// Inserisce nella coda ops la lista di file contenuti nella stringa arg in una
// struct operation con tipo op_type
int process_filelist(struct Queue *ops, char *arg, char op_type);
// Libera la struttura contenente le opzioni del client
void free_client_opt(struct client_opts *options);

// funzione per visitare ricorsivamente a partire da basedir al più nleft files
// I path dei file trovati sono inseriti nella coda pathlist ed il loro numero contenuto
// nella coda datalist è ritornato (o -1 per errore)
long int visit_dir(struct Queue *pathlist, struct Queue *datalist, const char *basedir, long int nleft);

#endif
