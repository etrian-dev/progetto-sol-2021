/**
 * \file logging.c
 * \brief File contenente l'implementazione delle funzioni di logging
 *
 * Questo file contiene l'implementazione di tutte le funzioni relative al logging: una
 * eseguita dal thread per il logging, una procedura per inserire facilmente nella coda di log un
 * nuovo messaggio ed infine una per tradurre (in modo MT-safe) da un codice di errore ad una stringa
 * che ne dettaglia la causa
 */

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

/**
 * \brief Funzione per ottenere a partire dal codice di errore (da errno) una stringa
 * \param [in] errcode Il codice di errore da tradurre, tipicamente > 0
 * \param [out] error_str Puntatore doppio alla stringa restituita, allocata dinamicamente all'interno della procedura
 * \return Se non ho modo di ottenere la stringa di errore error_str diviene NULL,
 * altrimenti è un puntatore al puntatore della stringa restituita (allocata con malloc)
 */
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


/// Minima dimensione del buffer in modo da garantire che possa contenere il timestamp
/// tradotto da ctime_t sotto forma di data ed ora leggibili
#define MINBUFSZ 26

/**
 * Il thread che la esegue ha il compito di effettuare la scrittura sul file di log dei
 * messaggi inseriti nella coda di richieste di log. Per farlo deve inizialmente creare
 * (o troncare) ed aprire il file di log con il path specificato nella struttura dati
 * per il logging (logging_params) in scrittura. Prima che il thread termini il file viene chiuso.
 * La funzione esegue ciclicamente le seguenti operazioni:
 * - Estrae dalla coda una richiesta di log (attende se la coda è vuota)
 * - Esamina la richiesta
 *  - Se di terminazione, cioè {-1, NULL} allora termina il thread
 *  - Altrimenti...
 *      - Prende il timestamp corrente e lo traduce in formato naturale (uso ctime_r)
 *      - Traduce un eventuale codice di errore (se != 0) nella stringa corrispondente chiamando get_errorstr()
 *      - Scrive sul file di log una riga con il seguente formato: [timestamp]: messaggio: stringa errore\n
 *
 * \param [in] params Il parametro passato dal thread manager alla creazione è un puntatore alla struttura dati condivisa del server (fs_ds_t)
 * \return Ritorna 0 se il thread termina normalmente (estrae una richiesta di terminazione), -1 altrimenti
 */
void *logging(void *params) {
    struct fs_ds_t *fs_params = (struct fs_ds_t *)params;
    struct logging_params *log_params = fs_params->log_thread_config;

    int logfile_fd;
    if((logfile_fd = open(log_params->log_fpath, O_WRONLY|O_CREAT|O_TRUNC)) == -1) {
        // fallita creazione file di log: notifico l'utente e svuoto la coda
        char errmsg[BUF_BASESZ];
        strerror_r(errno, errmsg, BUF_BASESZ);
        fprintf(stderr, "[LOGGING THREAD %lu]: Fallita creazione log file \"%s\": %s\n", pthread_self(), log_params->log_fpath, errmsg);
        return (void*)-1;
    }

    // Il formato dei record nel file di log è il seguente:
    // <data ed ora correnti>: <message>: <stringa corrispondente ad errcode>\n

	// alloco un numero di byte sufficiente per il timestamp prodotto da ctime_r
    char *timestamp = calloc(MINBUFSZ, sizeof(char)); // importante che sia una calloc in modo da avere sempre un terminatore
    if(!timestamp) {
        // errore di allocazione del buffer
        return (void*)-1;
    }

    // Loop principale: legge le richieste di log dalla coda e scrive sul file
    struct node_t *request = NULL;
    while(1) {
        LOCK_OR_KILL(fs_params, &(log_params->mux_logq), log_params->log_requests);
        // tento di estrarre nuove richieste dalla coda
        while((request = pop(log_params->log_requests)) == NULL) {
            // non ho richieste: attendo una nuova richiesta
            pthread_cond_wait(&(log_params->new_logrequest), &(log_params->mux_logq));
        }
        UNLOCK_OR_KILL(fs_params, &(log_params->mux_logq));

        struct log_request *log_req = (struct log_request *)request->data;

        // La terminazione del log thread è determinata dalla lettura di una richiesta di formato speciale
        // ovvero quella con codice di errore -1 e messaggio NULL
        if(log_req->error_code == -1 && log_req->message == NULL) {
            free(log_req);
            break;
        }

        time_t curr_time = time(NULL); // ottengo il tempo corrente
        if(ctime_r(&curr_time, timestamp) == NULL) {
            // errore nella scrittura della data
            free(timestamp);
            return (void*)-1;
        }
        timestamp[strlen(timestamp) - 1] = '\0';

        // Cerco di ottenere la stringa corrispondente al codice di errore (!= 0)
        char *error_str = NULL;
        if(log_req->error_code != 0) {
            get_errorstr(log_req->error_code, &error_str);
        }

        // Scrittura sul file di log della stringa col seguente formato:
        // [data + ora]: messaggio di log: stringa di errore (eventuale)
        dprintf(logfile_fd, "[%s]: %s", timestamp, log_req->message);
        if(log_req) {
            dprintf(logfile_fd, ": %s\n", error_str);
        }
        else {
            dprintf(logfile_fd, "\n");
        }

        free(log_req->message);
        free(log_req);
        free(request);
    }

    // Chiudo il file di log
    close(logfile_fd);

    // log thread terminato con successo
    return 0;
}

/**
 * \brief Funzione di utilità per inserire una richiesta di log in coda
 *
 * La funzione tenta di inserire nella coda contenuta in lp la richiesta di log avente come messaggio
 * la stringa message (duplicata internamente allo scopo di non dipendere dal chiamante per la
 * corretta gestione della memoria) e come codice di errore l'intero passato.
 * Se err == 0 indica che non è un errore di sistema, ma la volontà di aggiungere qualche
 * informazione al file di log (ad esempio che è stata compiuta una lettura di x bytes)
 * \param [in] lp Puntatore alla struttura che contiene i parametri per effettuare il logging
 * \param [in] err Codice di errore che sarà tradotto nella corrispondente stringa se necessario al momento del logging
 * \param [in] message Stringa che descrive l'evento che si vuole registrare. Viene duplicata internamente con strdup
 * \return La funzione ritorna 0 se ha successo, -1 altrimenti
 */
int put_logmsg(struct logging_params *lp, const int err, char *message) {
    // È ammesso avere un messaggio NULL sse è anche presente il codice di errore -1
    // Questo particolare insieme di parametri provoca la terminazione del thread di logging
    if(!(lp && lp->log_requests && (message || (err == -1 && !message)))) {
        return -1;
    }
    // creo una nuova richiesta ed inizializzo i suoi parametri
    struct log_request *newrequest = NULL;
    if((newrequest = malloc(sizeof(struct log_request))) == NULL) {
        return -1;
    }
    newrequest->error_code = err;
    newrequest->message = strdup(message);

    // prendo mutex sulla coda
    if(pthread_mutex_lock(&(lp->mux_logq)) != 0) {
        // fallito lock
        return -1;
    }
    if(enqueue(lp->log_requests, newrequest, sizeof(struct log_request), -1) == -1) {
        // fallito inserimento della richiesta di log in coda
        free(newrequest);
        return -1;
    }
    pthread_cond_signal(&(lp->new_logrequest));
    if(pthread_mutex_unlock(&(lp->mux_logq)) != 0) {
        // fallito unlock
        return -1;
    }
    return 0;
}

