// header server
#include <server-utils.h>
// header utilità
#include <utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> // per stampare il timestamp nel log

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
int logging(struct fs_ds_t *ds, int errcode, char *message) {
    // Il formato dei record nel file di log è il seguente:
    // <data ed ora correnti>: <message>: <stringa corrispondente ad errcode>\n

	// alloco un numero di byte sufficiente per il timestamp prodotto da ctime_r
    char *log_msg = calloc(BUF_BASESZ, sizeof(char)); // importante che sia una calloc in modo da avere sempre un terminatore
    if(!log_msg) {
        // errore di allocazione del buffer
        return -1;
    }
    size_t log_bufsz = BUF_BASESZ;

    // inizio la stringa con questo carattere
    log_msg[0] = '[';

    time_t curr_time = time(NULL); // ottengo il tempo corrente

    // scrive nel buffer (a partire dalla posizione 1) la data ed ora correnti
    if(ctime_r(&curr_time, &log_msg[1]) == NULL) {
        // errore nella scrittura della data
        free(log_msg);
        return -1;
    }
    // Lo \n inserito da ctime viene sostituito con ']' seguito da uno spazio
    char *end_date = strchr(log_msg, '\n');
    *end_date = ']';
    *(end_date + 1) = ' ';

    // Espando il buffer se necessario per scrivere il messaggio di log
    // la politica di espansione del buffer è raddoppiare la size
    // ho scelto questa politica in quanto i log saranno generalmente brevi,
    // perciò il loop sarà tipicamente eseguito un numero ridotto di volte
    size_t message_len = strlen(message);
    size_t log_len = strlen(log_msg);
    while(log_bufsz < log_len + message_len + 1) {
        if(rialloca_buffer(&log_msg, log_bufsz * 2) == -1) {
            free(log_msg);
            return -1;
        }
        memset(log_msg + log_bufsz, 0, log_bufsz * sizeof(char)); // azzero la porzione riallocata
        // aggiorno la size del buffer allocato
        log_bufsz = log_bufsz * 2;
    }
    // adesso concateno il messaggio
    if(strncat(log_msg, message, message_len + 1) == NULL) {
        // errore nella concatenazione
        free(log_msg);
        return -1;
    }
    log_len = strlen(log_msg); // aggiorno la lunghezza del messaggio di log

    // Cerco di ottenere la stringa solo se ho avuto un errore (quindi errcode != 0)
    char *error_str = NULL;
    if(errcode != 0) {
        get_errorstr(errcode, &error_str);
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
    log_msg[log_len + 1] = '\n';

    // Scivo nel file di log in ME
    if(pthread_mutex_lock(&(ds->mux_log)) == -1) {
        perror("Lock fallito");
    }
    if(writen(ds->log_fd, log_msg, log_len + 2) != log_len + 2) {
        // errore nella scrittura
        free(log_msg);
        return -1;
    }
    if(pthread_mutex_unlock(&(ds->mux_log)) == -1) {
        perror("Unlock fallito");
    }
    // libero il buffer
    free(log_msg);

    // operazione di logging terminata con successo
    return 0;
}

