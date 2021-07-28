// header server
#include <server-utils.h>
// header utilità
#include <utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

// File contenente l'implementazione della funzione per effettuare il logging delle operazioni
// effettuate dai client e dal server

// Funzione per ottenere a partire dal codice di errore (da errno) una stringa
// Se non ho modo di ottenere la stringa di errore error_str diviene NULL
void get_errorstr(int errcode, char **error_str) {
    if(!error_str) {
        // errore di programmazione passare direttamente NULL
        return;
    }
    // ottengo da strerror_r la stringa corrispondente a errcode
    // utilizzo quella con prototipo: int strerror_r(int errnum, char *buf, size_t buflen)
    *error_str = malloc(BUF_BASESZ * sizeof(char)); // BUF_BASESZ dovrebbe essere una lunghezza sufficiente per i messaggi di errore
    if(!(*error_str)) {
        // errore di allocazione
        return;
    }
    if(strerror_r(errcode, *error_str, BUF_BASESZ) != 0) {
        free(*error_str);
        *error_str = NULL;
    }
}

// Funzione per effettuare il logging: prende come parametri
// 1) il file descriptor (deve essere già aperto in scrittura) del file di log
// 2) il codice di errore (errno)
// 3) la stringa da stampare nel file (null-terminated)
// La funzione scrive sul file di log la stringa message, seguita dalla stringa corrispondente al codice di
// errore (se errcode != 0)
// La funzione ritorna 0 se ha successo e -1 se riscontra un errore (settato errno)
#define MINBUFSZ 26 // per via di ctime_r
void *logging(void *params) {
    struct logging_params *log_params = (struct logging_params *)params;

    int logfile_fd;
    if((logfile_fd = open(log_params->log_fpath, O_WRONLY|O_CREAT|O_TRUNC)) == -1) {
        // fallita creazione file di log: notifico l'utente e svuoto la coda
        char errmsg[BUF_BASESZ];
        strerror_r(errno, errmsg, BUF_BASESZ);
        fprintf(stderr, "[LOGGING THREAD %lu]: Fallita creazione log file \"%s\": %s\n", pthread_self(), log_params->log_fpath, errmsg);
        return (void*)1;
    }

    // Il formato dei record nel file di log è il seguente:
    // <data ed ora correnti>: <message>: <stringa corrispondente ad errcode>\n

	// alloco un numero di byte sufficiente per il timestamp prodotto da ctime_r
    char *timestamp = calloc(MINBUFSZ, sizeof(char)); // importante che sia una calloc in modo da avere sempre un terminatore
    if(!timestamp) {
        // errore di allocazione del buffer
        return (void*)1;
    }

    // Loop principale: legge le richieste di log dalla coda e scrive sul file
    struct node_t *request = NULL;
    while(1) {
        if(pthread_mutex_lock(&(log_params->mux_logq)) == -1) {
            ; // TODO: cleanup
        }
        // tento di estrarre nuove richieste dalla coda
        while((request = pop(log_params->log_requests)) == NULL) {
            // non ho richieste: attendo una nuova richiesta
            pthread_cond_wait(&(log_params->new_logrequest), &(log_params->mux_logq));
        }
        if(pthread_mutex_unlock(&(log_params->mux_logq)) == -1) {
            ; // TODO: cleanup
        }

        struct log_request *log_req = (struct log_request *)request;

        time_t curr_time = time(NULL); // ottengo il tempo corrente
        if(ctime_r(&curr_time, timestamp) == NULL) {
            // errore nella scrittura della data
            free(timestamp);
            return (void*)-1;
        }

        // Cerco di ottenere la stringa corrispondente al codice di errore (!= 0)
        char *error_str = NULL;
        if(log_req->error_code != 0) {
            get_errorstr(log_req->error_code, &error_str);
        }
        if(error_str) {
            // Se ho potuto ottenere la stringa di errore allora la concateno
            log_msg[log_len] = ':';
            log_len++;
            // resize del buffer se necessario
            size_t error_len = strlen(error_str);
            while(log_bufsz < log_len + error_len + 2) { // il +2 invece di +1 per aggiungere \n finale
                if(rialloca_buffer(&log_msg, log_bufsz * 2) == -1) {
                    free(log_msg);
                    return -1;
                }
                memset(log_msg + log_bufsz, 0, log_bufsz * sizeof(char)); // azzero la porzione riallocata
                // aggiorno la size del buffer allocato
                log_bufsz = log_bufsz * 2;
            }
            // adesso concateno il messaggio di errore
            if(strncat(log_msg, error_str, error_len + 1) == NULL) {
                // errore nella concatenazione
                free(log_msg);
                free(error_str);
                return -1;
            }
            // posso liberare la stringa di errore, dato che è stata concatenata
            free(error_str);
        }
        // Come ultimo carattere devo mettere uno \n
        log_len = strlen(log_msg);
        log_msg[log_len] = '\n';

        //
        dprintf(logfile_fd, "[%s] %s", timestamp, message);

        // Infine il messaggio è scritto nel file
        if(write(ds->log_fd, log_msg, log_len + 1) != log_len + 1) {
            // errore nella scrittura
            free(log_msg);
            return -1;
        }

        // libero il buffer
        free(log_msg);
    }

    // operazione di logging terminata con successo
    return 0;
}

// Funzione di utilità per inserire una richiesta di log in coda, avente come messaggio
// la stringa passata come parametro e come codice di errore l'intero passato
// (se err == 0 indica che non è un errore di sistema, ma logico di utilizzo del server)
// La funzione ritorna 0 se ha successo (è stata inserita la richiesta in coda e sarà
// scritta sul file di log appena possibile), -1 altrimenti
int put_logmsg(struct logging_params *lp, const int err, char *message) {
    if(!(lp && lp->log_requests && message)) {
        return -1;
    }
    // creo una nuova richiesta ed inizializzo i suoi parametri
    struct log_request *newrequest = NULL;
    if((newrequest = malloc(sizeof(struct log_request))) == NULL) {
        return -1;
    }
    newrequest->error_code = err;
    newrequest->message = message;

    // prendo mutex sulla coda
    if(pthread_mutex_lock(&(lp->mux_logq) != 0) {
        // fallito lock
        return -1;
    }
    if(enqueue(lp->log_requests, newrequest, sizeof(struct log_request), -1) == -1) {
        // fallito inserimento della richiesta di log in coda
        free(newrequest);
        return -1;
    }
    if(pthread_mutex_unlock(&(lp->mux_logq) != 0) {
        // fallito unlock
        return -1;
    }
    return 0;
}

