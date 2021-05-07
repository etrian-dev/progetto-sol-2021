// header del client, contente definizioni di funzione e macro utili al client
#ifndef CLIENT_H_INCLUDED
#define CLIENT_H_INCLUDED

// definizione delle opzioni riconosciute dal client
// -h: stampa la lista di tutte le opzioni accettate
// -f <fname>: specifica il nome del socket per la comunicazione con il server
// -w <dirname>[,n=0]: il client invia al server richieste di lettura dei file nella directory
// 	dirname e ricorsivamente le directory figlie, fino a n file (se n==0 o non specificato
// 	allora visita si arresta solo se non vi sono più subdirectory da visitare
// -W <file1> [, <filen>]: Invia al server la richiesta di scrivere la lista di file passati
// -D <dirname>: la cartella dove vengono scritti i file inviati dal server a seguito di
// 	rimpiazzamenti. Se viene specificata -D, allora devono essere specificate anche -w o -W
// -r <file1> [, <filen>]: lista di file che il client vuole leggere
// -R [n=0]: opzione per leggere n file nel server (qualsiasi essi siano). Se n==0 o non
//	specificato allora viene inviata la richiesta di leggere tutti i file presenti
// -d <dirname>: specifica la directory dove salvare i file letti (con -r o -R). Se non
// 	sono passate anche -r o -R insieme a -d si ha errore
// -t <time>: tempo in ms che intercorre tra due richieste al server (se time==0 allora
// 	non vi è ritardo aggiuntivo tra due richieste
// -l <file1> [, <filen>]: lista di file su cui il client richiede la mutua esclusione
// -u <file1> [, <filen>]: lista di file su cui il client richiede il rilascio della mutua esclusione
// -c <file1> [, <filen>]: lista di file da rimuovere dal server (se presenti)
// -p: Stampa sullo standard output ogni operazione effettuata dal client
#define CLIENT_OPSTRING ":hf:w:D:R::d:t:p"
// i due punti iniziali per distinguere tra opzione non riconosciuta e argomento mancante

// definisco una struttura che conterrà i valori delle opzioni specificate
struct client_opts {
    short int help_on;                // default: 0 (non stampa il messaggio di help)
    char *fs_socket;                  // default: NULL
    char *dir_write;                  // default: NULL
    int max_write;                    // default: -1 (interpretato come nessun limite)
    char **write_arr;                 // default: NULL
    char *dir_swapout;                // default: NULL
    char **read_arr;                  // default: NULL
    int max_read;                     // default: -1 (interpretato come nessun limite)
    char *dir_save_reads;             // default: NULL
    unsigned long rdelay;             // default: 0 (interpretato come numero di millisecondi)
    char **lock_arr;                  // default: NULL
    char **unlock_arr;                // default: NULL
    char **rm_arr;                    // default: NULL
    short int prints_on;              // default: 0 (non stampa le operazioni su stdout)
};

int get_client_options(int nargs, char **args, struct client_opts *params);

#endif
