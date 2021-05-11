// header api
#include <fs-api.h>
// hash table header
#include "icl_hash.h"
// syscall headers
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
// std headers
#include <time.h>

// file contenente l'implementazione della api di comunicazione tra file storage server ed i client

// semplice funzione di confronto tra interi: -1 se a < b; 0 se a == b; 1 se a > b
int cmp_int(void *a, void *b) { return (int)a - (int)b; }

// funzione di utilità per convertire msec in un delay specificato secondo timespec
void get_delay(const int msec, struct timespec *delay) {
    // dato che il delay è specificato in ms devo convertirlo a ns per scriverlo in timespec
    // Se il delay in ms è troppo grande (>=1000) per essere convertito in ns provo a convertire in secondi
    // Se non gestissi questi due casi potrei avere overflow su tv_nsec
    else if(msec >= 1000) {
	delay->tv_sec = (time_t)(msec / 1000); // con la divisione intera ottengo il numero di secondi in msec ms
	delay->tv_nsec = (msec % 1000) * 1000000; // il resto è convertito a ns e non posso avere overflow
    }
    else {
	delay->tv_nsec = msec * 1000000;
    }
}

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
    if((conn_sock = socket(AF_UNIX, SOCK_STREAM, 0) == -1) {
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
	memset(address, 0, sizeof(struct sockaddr_un)); // azzero per sicurezza

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

	}
    } while(!term);

    // ripristino in errno l'ultimo errore incontrato (per clock o connect)
    errno = errno_saved;
    // Ritorno -1 al chiamante
    return -1;
}

// chiude la connessione a sockname
int closeConnection(const char *sockname) {

}


