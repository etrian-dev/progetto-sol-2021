// header api
#include <fs-api.h>
#include <utils.h>
// syscall headers
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
// std headers
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

// file contenente l'implementazione della api di comunicazione tra file storage server ed i client

// Apre la connessione al socket sockname
int openConnection(const char *sockname, int msec, const struct timespec abstime) {
    if(msec < 0) {
	// errore: il delay deve essere non negativo
	return -1;
    }
    // converto il delay da msec in struct timespec
    struct timespec delay;
    get_delay(msec, &delay); // non fallisce

    // Il socket su cui connettersi è AF_UNIX e di tipo SOCK_STREAM
    int conn_sock;
    if((conn_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
	// errore nella creazione del socket: ritorno -1 ed errno sarà settato all'errore corrispondente
	return -1;
    }
    // preparo la struttura per contenere l'indirizzo del socket
    struct sockaddr_un address;
    memset(&address, 0, sizeof(struct sockaddr_un)); // azzero per sicurezza
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, sockname, SOCK_PATH_MAXLEN);

    struct timespec curr_time;
    int errno_saved;
    int term = 0;
    // tenta di connettersi al socket aspettando delay msec e fallisce se non è stato possibile entro abstime
    do {
	if(clock_gettime(CLOCK_REALTIME, &curr_time) == -1) {
	    // errore nel leggere il tempo corrente: salvo errno ed esco
	    errno_saved = errno;
	    close(conn_sock); // dato che sto uscendo ho comunque un problema,
	    // quindi anche se close fallisse non mi aspetto di riprovarla
	    errno = errno_saved;
	    return -1;
	}
	if(curr_time.tv_sec > abstime.tv_sec || curr_time.tv_nsec >= abstime.tv_nsec) {
	    // esco perchè ho raggiunto (o superato) il tempo assoluto
	    term = 1;
	}

	struct timespec sleep_left;
	if(connect(conn_sock, (struct sockaddr*)&address, sizeof(struct sockaddr_un)) == -1) {
	    // salvo errno generato da connect perchè potrebbe essere sovrascritto
	    errno_saved = errno;
	    // riprovo dopo aver aspettato il tempo delay
	    if(nanosleep(&delay, &sleep_left) == -1) {
		if(errno == EINTR) {
		    // nanosleep è stata interrotta da qualche segnale
		    ; // TODO: cosa fare?
		}
	    }
	}
	// Altrimenti la connessione ha avuto successo: devo inserire il client
	// nella struttura dati della API che memorizza le connessioni
	else {
	    if(!clients_info) {
		// è il primo client connesso al server, per cui devo inizializzare clients_info
		if(init_api(sockname) == -1) {
		    errno_saved = errno; // salvo l'errore
		    close(conn_sock); // tento di chiudere il socket aperto
		    errno = errno_saved;
		    return -1;
		}
	    }
	    // tutto ok: connessione tra client e server stabilita
	    return 0;
	}
    } while(!term);
    // Tempo scaduto (curr_time >= abstime)
    // ripristino in errno l'ultimo errore incontrato
    errno = errno_saved;

    return -1;
}

// Chiude la connessione a sockname relativa al client chiamante
int closeConnection(const char *sockname) {
    // prima controllo che il nome del socket sia quello giusto
    if(strncmp(clients_info->sockname, sockname, strlen(sockname)) == 0) {
	// Devo sapere il PID del chiamante per cercarlo tra le connessioni aperte
	int cpid = getpid();
	if(rm_client(cpid) == -1) {
	    // errore: client non connesso o impossibile chiudere correttamente il socket
	    return -1;
	}
	// Rimozione OK
	return 0;
    }

    // è stato richiesto di chiudere la connessione su un socket che non è quello
    // su cui tale connessione è stata aperta, quindi l'operazione fallisce
    return -1;
}

// Apre il file pathname (se presente nel server e solo per il client che la invia)
int openFile(const char *pathname, int flags) {
    // Verifico che questo client sia connesso
    int cPID = getpid();
    int pos;
    if((pos = isConnected(cPID)) == -1) {
	// errore: client non connesso
	return -1;
    }
    // pos ora contiene la posizione del client, per cui posso accedere a clients_info
    int csock = clients_info->client_id[2 * pos];

    // preparo la richiesta
    struct request_t *request = newrequest(OPEN_FILE, flags, strlen(pathname) + 1, 0, 0);
    if(!request) {
	// errore di allocazione
	return -1;
    }
    // Scrivo la richiesta sul socket
    if(writen(csock, request, sizeof(struct request_t)) == -1) {
	return -1;
    }
    if(writen(csock, pathname, strlen(pathname) + 1) == -1) {
	return -1;
    }

    // Richiesta inviata: attendo risposta
    struct reply_t *reply = malloc(sizeof(struct reply_t));
    if(!reply) {
	return -1;
    }
    if(readn(csock, reply, sizeof(struct reply_t)) == -1) {
	// errore nella risposta (che non sia EINTR)
	if(errno != EINTR) {
	    return -1;
	}
    }
    if(reply->status != REPLY_YES) {
	// errore: la richiesta non è stata soddisfatta
	return -1;
    }
    free(reply); // il buffer in questo caso è sempre NULL

    // richiesta OK: file aperto
    return 0;
}

// invia al server la richiesta di chiusura del file pathname (solo per il client che la invia)
int closeFile(const char *pathname) {
    // Verifico che questo client sia connesso
    int cPID = getpid();
    int pos;
    if((pos = isConnected(cPID)) == -1) {
	// errore: client non connesso
	return -1;
    }
    // pos ora contiene la posizione del client, per cui posso accedere a clients_info
    int csock = clients_info->client_id[2 * pos];

    // preparo la richiesta
    struct request_t *request = newrequest(CLOSE_FILE, 0, strlen(pathname) + 1, 0, 0);
    if(!request) {
	// errore di allocazione
	return -1;
    }
    // Scrivo la richiesta sul socket
    if(writen(csock, request, sizeof(struct request_t)) == -1) {
	return -1;
    }
    if(writen(csock, pathname, strlen(pathname) + 1) == -1) {
	return -1;
    }

    // Richiesta inviata: attendo risposta
    struct reply_t *reply = malloc(sizeof(struct reply_t));
    if(!reply) {
	return -1;
    }
    if(readn(csock, reply, sizeof(struct reply_t)) == -1) {
	// errore nella risposta (che non sia EINTR)
	if(errno != EINTR) {
	    return -1;
	}
    }
    if(reply->status != REPLY_YES) {
	// errore: la richiesta non è stata soddisfatta
	return -1;
    }
    free(reply); // il buffer in questo caso è sempre NULL

    // richiesta OK: file chiuso
    return 0;
}

// Invia al server la richiesta di lettura del file pathname, ritornando un puntatore al buffer
int readFile(const char *pathname, void **buf, size_t *size) {
    // controllo che sia stato passato un indirizzo di buffer non nullo
    if(!buf) {
	return -1;
    }
    // controllo che questo client sia connesso
    int pos = -1;
    int pid = getpid(); // prendo il pid del client chiamante
    if((pos = isConnected(pid)) == -1) {
	// errore: client non connesso
	return -1;
    }
    // ottengo il suo socket
    int csock = clients_info->client_id[2 * pos];

    // preparo la richiesta (flags non sono utilizzate, quindi passo 0
    struct request_t *request = newrequest(READ_FILE, 0, strlen(pathname) + 1, 0, 0);
    if(!request) {
	// errore di allocazione
	return -1;
    }
    // Scrivo la richiesta sul socket
    if(writen(csock, request, sizeof(struct request_t)) == -1) {
	return -1;
    }
    if(writen(csock, pathname, strlen(pathname) + 1) == -1) {
	return -1;
    }

    // attendo la risposta
    struct reply_t *reply = malloc(sizeof(struct reply_t));
    if(!reply) {
	return -1;
    }
    if(read(csock, reply, sizeof(struct reply_t)) == -1) {
	// errore nella risposta
	return -1;
    }
    if(reply->status != REPLY_YES) {
	// operazione negata
	return -1;
    }
    // altrimenti lettura consentita
    *buf = malloc(reply->buflen);
    if(!(*buf)) {
	return -1;
    }
    if(readn(csock, *buf, reply->buflen) == -1) {
	// errore nella risposta
	return -1;
    }
    *size =reply->buflen;

    // File letto con successo
    return 0;
}

// legge n files qualsiasi dal server (nessun limite se n<=0) e se non è nullo allora li salva in dirname
int readNFiles(int N, const char *dirname) {
    return -1;
}

// Scrive in append al file pathname il contenuto di buf
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname) {
    // controllo che buf sia non nullo e size > 0
    if(!buf || size < 0) {
	return -1;
    }

    // controllo che questo client sia connesso
    int pos = -1;
    int pid = getpid(); // prendo il pid del client chiamante
    if((pos = isConnected(pid)) == -1) {
	// errore: client non connesso
	return -1;
    }
    // ottengo il suo socket
    int csock = clients_info->client_id[2 * pos];

    // preparo la richiesta (flags non utilizzate)
    size_t dirname_len = (dirname == NULL ? 0 : strlen(dirname) + 1);
    struct request_t *request = newrequest(APPEND_FILE, 0, strlen(pathname) + 1, size, dirname_len);
    if(!request) {
	// errore di allocazione
	return -1;
    }
    // Scrivo la richiesta sul socket
    if(writen(csock, request, sizeof(struct request_t)) == -1) {
	return -1;
    }
    if(writen(csock, pathname, strlen(pathname) + 1) == -1) {
	return -1;
    }
    if(writen(csock, buf, size) == -1) {
	return -1;
    }
    if(writen(csock, dirname, dirname_len) == -1) {
	return -1;
    }

    // attendo la risposta
    struct reply_t *reply = malloc(sizeof(struct reply_t));
    if(!reply) {
	return -1;
    }
    if(read(csock, reply, sizeof(struct reply_t)) == -1) {
	// errore nella risposta
	return -1;
    }
    if(reply->status != REPLY_YES) {
	// operazione negata
	return -1;
    }

    // append consentito: guardo se sono stati espulsi dei file (buf non nullo)
    if(reply->buflen > 0) {
	puts("Espulsi dei file");
    }
    else {
	puts("Nessun file espulso");
    }

    return 0;
}

// invia al server la richiesta di scrittura del file pathname
int writeFile(const char *pathname, const char *dirname) {
    return -1;
}
