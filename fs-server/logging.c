// header progetto
#include <utils.h>
#include <server-utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <time.h> // per stampare l'orario nel log

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
    // utilizzo quella con dichiarazione: int strerror_r(int errnum, char *buf, size_t buflen)
    *error_str = malloc(BUF_BASESZ * sizeof(char));
    if(!error_str) {
	// errore di allocazione
	error_str= NULL;
	return;
    }
    if(strerror_r(errcode, *error_str, BUF_BASESZ) != 0) {
	free(error_str);
	error_str = NULL;
    }
}

// Funzione per effettuare il logging: prende come parametri
// 1) il file descriptor (deve essere già aperto in scrittura) del file di log
// 2) il codice di errore (errno)
// 3) la stringa da stampare nel file (null-terminated)
// La funzione ritorna:
// 0 se ha successo
// -1 se riscontra un errore (settato errno)
int log(struct fs_ds_t *ds, int errcode, char *message) {
    // Il formato dei record nel file di log è il seguente:
    // [data ed ora correnti] <message>: <stringa corrispondente ad errcode>\n

    char *log_msg = calloc(BUF_BASESZ, sizeof(char));
    if(!log_msg) {
	// errore di allocazione del buffer
	return -1;
    }
    size_t log_bufsz = BUF_BASESZ;

    // inizio la stringa con questo carattere
    log_msg[0] = '[';

    time_t curr_time = time(0);

    // scrive nel buffer (a partire dalla posizione 1)
    // la data ed ora correnti (il buffer usato deve essere lungo almeno 26 caratteri)
    if(ctime_r(&curr_time, log_msg + 1) == NULL) {
	// errore nella scrittura della data
	return -1;
    }
    // ctime inserisce anche '\n' ed io lo sostituisco con ']'
    char *end_date = strrchr(log_msg, '\n');
    if(!end_date) {
	*end_date = ']';
    }

    // resize del buffer se necessario
    size_t message_len = strlen(message);
    size_t log_len = strlen(log_msg); // devo sapere quanti caratteri ho già scritto nel buffer
    while(log_bufsz < log_len + message_len + 1) {
	// la politica di espansione del buffer è raddoppiare la size
	// ho scelto questa politica in quanto i log saranno generalmente brevi,
	// perciò il loop sarà tipicamente eseguito un numero ridotto di volte
	if(rialloca_buffer(&log_msg, log_bufsz * 2) == -1) {
	    // errore nella riallocazione
	    free(log_msg);
	    return -1;
	}
	// aggiorno la size del buffer allocato
	log_bufsz = log_bufsz * 2;
    }
    // adesso concateno il messaggio
    if(strncat(log_msg, message, message_len + 1)) {
	// errore nella concatenazione
	free(log_msg);
	return -1;
    }

    // anche se non ho modo di ottenere la stringa di errore scrivo comunque il resto del log
    char *error_str = NULL;
    get_errorstr(errcode, &error_str);
    if(error_str) {
	// ha avuto successo la funzione, per cui la concateno
	// resize del buffer se necessario
	size_t error_len = strlen(error_str);
	log_len = strlen(log_msg); // devo sapere quanti caratteri ho già scritto nel buffer
	while(log_bufsz < log_len + error_len + 1) {
	    // la politica di espansione del buffer è raddoppiare la size
	    // ho scelto questa politica in quanto i log saranno generalmente brevi,
	    // perciò il loop sarà tipicamente eseguito un numero ridotto di volte
	    if(rialloca_buffer(&log_msg, log_bufsz * 2) == -1) {
		// errore nella riallocazione
		free(log_msg);
		return -1;
	    }
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
	// libero questa stringa, dato che è stata concatenata
	free(error_str);
    }

    if(pthread_mutex_lock(&(ds->mux_log)) == -1) {
	perror("Lock fallito");
    }

    // messaggio di log preparato, ora lo scrivo
    if(write(ds->log_fd, log_msg, log_bufsz) == -1) { // writen?
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

