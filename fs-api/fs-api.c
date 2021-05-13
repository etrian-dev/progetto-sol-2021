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

// file contenente l'implementazione della api di comunicazione tra file storage server ed i client
// TODO: variare codice errore per msg diagnostica client

struct conn_info *clients_info = NULL;

// apre la connessione al socket sockname, su cui il server sta ascoltando
int openConnection(const char *sockname, int msec, const struct timespec abstime) {
    if(msec < 0) {
	// errore: il delay deve essere non negativo, perciò ritorno -1
	return -1;
    }
    // preparo la struttura per specificare il delay dei tentativi di connessione
    struct timespec delay;
    get_delay(msec, &delay);

    // Il socket su cui connettersi è AF_UNIX e di tipo SOCK_STREAM
    int conn_sock;
    if((conn_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
	// errore nella creazione del socket: ritorno -1 ed errno sarà settato
	return -1;
    }

    // tenta di connettersi al socket ogni msec e fallisce se non è stato possibile entro abstime
    struct timespec curr_time;
    int errno_saved;
    int term = 0;
    do {
	if(clock_gettime(CLOCK_REALTIME, &curr_time) == -1) {
	    // errore nel leggere il tempo corrente: salvo errno, poi cleanup ed esco con errore
	    errno_saved = errno;
	    close(conn_sock); // dato che sto uscendo ho comunque un problema, quindi anche se close fallisse non mi aspetto di riprovarla
	    errno = errno_saved;
	    return -1;
	}
	if(curr_time.tv_sec > abstime.tv_sec || curr_time.tv_nsec >= abstime.tv_nsec) {
	    // esco perchè ho raggiunto (o superato) il tempo assoluto
	    term = 1;
	}

	// preparo la struttura per contenere l'indirizzo del socket
	struct sockaddr_un address;
	memset(&address, 0, sizeof(struct sockaddr_un)); // azzero per sicurezza

	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, sockname, SOCK_PATH_MAXLEN); // TODO: controllo errori

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
	    // altrimenti riprovo a connettere il client se non ho superato abstime
	}
	// Altrimenti la connessione ha avuto successo: effettuo delle operazioni prima di ritornare 0
	else {
	    if(!clients_info) {
		// è il primo client che tenta di connettersi al server, per cui devo inizializzare
		// la struttura dati della API
		if(init_api(sockname) == -1) {
		    errno_saved = errno; // salvo l'errore
		    close(conn_sock); // tento di chiudere il socket aperto
		    errno = errno_saved;
		    return -1;
		}
	    }
	    // Aggiungo il client alla struttura dati, passando il socket
	    // Internamente usa il PID per distinguere tra i vari client
	    if(add_client(conn_sock) == -1) {
		return -1;
	    }

	    // tutto ok: connessione tra client e server stabilita
	    return 0;
	}
    } while(!term);

    // ripristino in errno l'ultimo errore incontrato (per clock o connect)
    errno = errno_saved;
    // Ritorno -1 al chiamante
    return -1;
}

// chiude la connessione a sockname relativa al client chiamante
int closeConnection(const char *sockname) {
    // prima controllo che il nome del socket sia quello giusto
    if(strncmp(clients_info->sockname, sockname, strlen(sockname)) == 0) {
	// Provo a rimuovere questo client, sapendo il suo PID
	int cpid = getpid();

	// resetto errno per capire se vi sia stato un eventuale errore di chiusura del socket
	errno = 0;
	if(rm_client(cpid) == -1) {
	    if(errno != 0) {
		// Fallita la chiusura del socket
		perror("Fallita chiusura del socket (client rimosso)");
	    }
	    else {
		// Fallita la rimozione perché il client non era connesso
		return -1;
	    }
	}
	// Rimozione OK
	return 0;
    }
    // è stato richiesto di chiudere la connessione su un socket che non è quello
    // su cui tale connessione è stata aperta, quindi l'operazione fallisce
    return -1;
}

// invia al server la richiesta di lettura del file pathname, ritornando un puntatore al buffer
int readFile(const char *pathname, void **buf, size_t *size) {
    if(!(buf && !(*buf)) {
	return -1;
    }

    // Cerco il client nell'array
    size_t i = 0;
    int nfound = 1;
    int sock = -1;
    while(nfound && i < clients_info->capacity) {
	int this_pid = getpid();
	if(clients_info->client_id[2 * i + 1] == this_pid) {
	    sock = clients_info->client_id[2 * i];
	    nfound = 0;
	}
	i++;
    }
    if(nfound) {
	// non è stato trovato, quindi non era connesso
	return -1;
    }
    // Scrivo sul socket la richiesta di lettura ed il pathname, poi attendo la risposta
    size_t req_sz = strlen(pathname) + 3; // la richiesta ha il "formato R\0<pathname>\0"
    char *req_buf = NULL;
    if((req_buf = malloc(req_sz * sizeof(char))) == NULL) {
	// errore di allocazione
	return -1;
    }
    int nwritten = snprintf(req_buf, req_sz, "R%c%s%c", '\0', pathname, '\0');
    if(nwritten < 0 || nwritten > req_sz) {
	// errore nella scrittura sul buffer
	free(req_buf);
	return -1;
    }
    // scrivo la richiesta
    if(writen(sock, req_buf, req_sz) != req_sz) {
	// errore nella scrittura
	free(req_buf);
	return -1;
    }
    // posso liberare il buffer
    free(req_buf); req_buf = NULL; req_sz = 0;

    // attendo in modo bloccante la risposta
}


