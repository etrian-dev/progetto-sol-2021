// header api
#include <fs-api.h>
// header utilità
#include <utils.h>
// syscall headers
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
// std headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

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
            // Devo anche inviare il PID del client connesso al server
            int client_pid = getpid();
            if(writen(conn_sock, &client_pid, sizeof(int)) != sizeof(int)) {
                // fallita scrittura PID: connessione chiusa lato server ed operazione fallisce
                return -1;
            }
            // aggiungo il client
            if(!clients_info) {
                // è il primo client connesso al server, per cui devo inizializzare clients_info
                if(init_api(sockname) == -1) {
                    errno_saved = errno; // salvo l'errore
                    close(conn_sock); // tento di chiudere il socket aperto
                    errno = errno_saved;
                    return -1;
                }
            }
            if(add_client(conn_sock, client_pid) == -1) {
                // fallito inserimento del client: impossibile aprire connnessione
                return -1;
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
    if(sockname && strncmp(clients_info->sockname, sockname, strlen(sockname)) == 0) {
       // Verifico che il client sia connesso al server
        int csock = isConnected();
        if(csock == -1) {
            return -1;
        }

        // preparo la richiesta
        struct request_t *request = newrequest(CLOSE_CONN, 0, 0, 0);
        if(!request) {
            // errore di allocazione
            return -1;
        }

        // Scrivo la richiesta sul socket
        if(writen(csock, request, sizeof(struct request_t)) != sizeof(struct request_t)) {
            // errore di scrittura della richiesta
            free(request);
            return -1;
        }

        // Rimuovo traccia della connessione anche nella API
        int myPID = getpid();
        if(rm_client(myPID) == -1) {
            // errore: impossibile chiudere correttamente il socket
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
    // Verifico che il client sia connesso al server
    int csock = isConnected();
    if(csock == -1) {
        return -1;
    }

    // preparo la richiesta
    struct request_t *request = newrequest(OPEN_FILE, flags, strlen(pathname) + 1, 0);
    if(!request) {
        // errore di allocazione
        return -1;
    }
    // Scrivo la richiesta sul socket
    if(writen(csock, request, sizeof(struct request_t)) != sizeof(struct request_t)) {
        return -1;
    }
    size_t slen = strlen(pathname) + 1;
    if(writen(csock, pathname, slen) != slen) {
        return -1;
    }

    // Richiesta inviata: attendo risposta
    struct reply_t *reply = malloc(sizeof(struct reply_t));
    if(!reply) {
        return -1;
    }
    if(readn(csock, reply, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
        return -1;
    }
    if(reply->status != REPLY_YES) {
        // errore: la richiesta non è stata soddisfatta
        free(reply);
        return -1;
    }
    // ad openFile non viene restituito alcun buffer, per cui posso liberare memoria ed uscire
    free(reply);

    // richiesta OK: file aperto
    return 0;
}

// invia al server la richiesta di chiusura del file pathname (solo per il client che la invia)
int closeFile(const char *pathname) {
    // Verifico che il client sia connesso al server
    int csock = isConnected();
    if(csock == -1) {
        return -1;
    }

    // preparo la richiesta
    struct request_t *request = newrequest(CLOSE_FILE, 0, strlen(pathname) + 1, 0);
    if(!request) {
        // errore di allocazione
        return -1;
    }
    // Scrivo la richiesta sul socket
    if(writen(csock, request, sizeof(struct request_t)) != sizeof(struct request_t)) {
        return -1;
    }
    size_t slen = strlen(pathname) + 1;
    if(writen(csock, pathname, slen) != slen) {
        return -1;
    }

    // Richiesta inviata: attendo risposta
    struct reply_t *reply = malloc(sizeof(struct reply_t));
    if(!reply) {
        return -1;
    }
    if(readn(csock, reply, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
        return -1;
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
    // Verifico che il client sia connesso al server
    int csock = isConnected();
    if(csock == -1) {
        return -1;
    }

    // preparo la richiesta (flags non sono utilizzate, quindi passo 0)
    struct request_t *request = newrequest(READ_FILE, 0, strlen(pathname) + 1, 0);
    if(!request) {
        // errore di allocazione
        return -1;
    }
    // Scrivo la richiesta sul socket
    if(writen(csock, request, sizeof(struct request_t)) != sizeof(struct request_t)) {
        return -1;
    }
    size_t slen = strlen(pathname) + 1;
    if(writen(csock, pathname, slen) != slen) {
        return -1;
    }

    // attendo la risposta
    struct reply_t *reply = malloc(sizeof(struct reply_t));
    if(!reply) {
        return -1;
    }
    if(readn(csock, reply, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
        // errore nella risposta
        return -1;
    }
    if(reply->status == REPLY_NO) {
        // operazione negata
        return -1;
    }
    // altrimenti lettura consentita
    // NOTA: la dimensione del file è inserita direttamente in paths_sz per evitare una ulteriore read
    *buf = malloc(reply->paths_sz);
    if(!(*buf)) {
        return -1;
    }
    // leggo il file mandato dal server nel buffer appena allocato
    if(readn(csock, *buf, reply->paths_sz) != reply->paths_sz) {
        // errore nella lettura (errno settato)
        free(buf);
        return -1;
    }
    // setto la size
    *size =reply->paths_sz;

    free(reply);

    // File letto con successo: buf contiene i dati e size la dimesione di buf
    return 0;
}

// legge n files qualsiasi dal server (nessun limite se n<=0) e
// se dirname non è NULL allora li salva in dirname utilizzando come nome file il loro path nel server
int readNFiles(int N, const char *dirname) {
    // Verifico che il client sia connesso al server
    int csock = isConnected();
    if(csock == -1) {
        return -1;
    }

    // Invio n richieste: uso il campo flags (intero) per specificarne il numero
    struct request_t *req = newrequest(READ_N_FILES, N, 0, 0);
    if(!req) {
        // fallita allocazione richiesta
        return -1;
    }

    // Scrivo la richiesta di lettura di n files
    if(writen(csock, req, sizeof(struct request_t)) != sizeof(struct request_t)) {
        // fallita scrittura richiesta
        free(req);
        return -1;
    }
    free(req);

    // Attendo la risposta
    struct reply_t *rep = malloc(sizeof(struct reply_t));
    if(!rep) {
        free(req);
        return -1;
    }
    if(readn(csock, rep, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
        // fallita ricezione riposta
        free(req);
        return -1;
    }
    if(rep->status == REPLY_NO) {
        // Operazione negata
        free(rep);
        return -1;
    }
    // leggo la dimensione dei file restituiti in un array
    size_t *sizes = malloc(rep->nbuffers * sizeof(size_t));
    if(!sizes) {
        // fallita allocazione
        free(rep);
        return -1;
    }
    if(readn(csock, sizes, rep->nbuffers * sizeof(size_t)) != rep->nbuffers * sizeof(size_t)) {
        // fallita lettura dimensioni file
        free(sizes);
        free(rep);
        return -1;
    }
    // leggo i path dei file ricevuti (di cui conosco la lunghezza totale)
    char *paths = malloc(rep->paths_sz);
    if(!paths) {
        // errore di allocazione
        free(sizes);
        free(rep);
        return -1;
    }
    if(readn(csock, paths, rep->paths_sz) != rep->paths_sz) {
        // lettura path incompleta
        free(paths);
        free(sizes);
        free(rep);
        return -1;
    }

    // Se dirname != NULL scrive i file che riceve dal socket in tale directory
    // altrimenti tali file sono liberati
    int num_docs = write_swp(csock, dirname, rep->nbuffers, sizes, paths);

    // Libero memoria
    free(sizes);
    free(paths);
    free(rep);

    // l'operazione ritorna il numero di file letti con successo, -1 altrimenti
    return num_docs;
}

// Scrive in append al file pathname il contenuto di buf
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname) {
    // controllo che l'indirizzo di buf sia non nullo e size > 0
    if(!buf || size <= 0) {
        return -1;
    }
    // Verifico che il client sia connesso al server
    int csock = isConnected();
    if(csock == -1) {
        return -1;
    }

    // preparo la richiesta (flags non utilizzate)
    struct request_t *request = newrequest(APPEND_FILE, 0, strlen(pathname) + 1, size);
    if(!request) {
        // errore di allocazione
        return -1;
    }
    // Scrivo la richiesta sul socket
    if(writen(csock, request, sizeof(struct request_t)) != sizeof(struct request_t)) {
        free(request);
        return -1;
    }
    // Di seguito scrivo il path del file da modificare ed i dati da concatenare
    size_t slen = strlen(pathname) + 1;
    if(writen(csock, pathname, slen) != slen) {
        free(request);
        return -1;
    }
    if(writen(csock, buf, size) == -1) {
        free(request);
        return -1;
    }
    free(request);

    // Attendo la risposta
    struct reply_t *rep = malloc(sizeof(struct reply_t));
    if(!rep) {
        return -1;
    }
    if(readn(csock, rep, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
        // errore nella risposta
        free(rep);
        return -1;
    }
    if(rep->status == REPLY_NO) {
        // operazione negata
        free(rep);
        return -1;
    }

    // Operazione consentita
    // Controllo se ho avuto delle espulsioni di file
    if(rep->nbuffers > 0) {
        // leggo le dimensioni dei file ricevuti
        size_t *sizes = malloc(rep->nbuffers * sizeof(size_t));
        if(!sizes) {
            // fallita allocazione
            free(rep);
            return -1;
        }
        if(readn(csock, sizes, rep->nbuffers * sizeof(size_t)) != rep->nbuffers * sizeof(size_t)) {
            // fallita lettura
            free(sizes);
            free(rep);
            return -1;
        }

        // leggo i path dei file ricevuti (di cui conosco la lunghezza totale)
        char *paths = malloc(rep->paths_sz);
        if(!paths) {
            // errore di allocazione
            free(sizes);
            free(rep);
            return -1;
        }
        if(readn(csock, paths, rep->paths_sz) != rep->paths_sz) {
            // lettura path incompleta
            free(sizes);
            free(paths);
            free(rep);
            return -1;
        }

        int num_docs = -1; // write_swp restituisce il numero di file ricevuti e scritti in dirname
        num_docs = write_swp(csock, dirname, rep->nbuffers, sizes, paths);
        if(num_docs == -1) {
            // Qualcosa ha fallito
            free(sizes);
            free(paths);
            free(rep);
            return -1;
        }
        free(sizes);
        free(paths);
    } // Altrimenti nessun file è stato espulso

    // libero memoria
    free(rep);

    return 0;
}

// Invia al server la richiesta di scrittura (creazione) del file pathname
int writeFile(const char *pathname, const char *dirname) {
    // Verifico che il client sia connesso al server
    int csock = isConnected();
    if(csock == -1) {
        return -1;
    }

    // Apro il file con path pathname e ne leggo i dati in un buffer
    int file_fd;
    if((file_fd = open(pathname, O_RDONLY)) == -1) {
        // Fallita apertura del file: ritorno errore al client
        return -1;
    }
    struct stat info;
    memset(&info, 0, sizeof(struct stat));
    if(fstat(file_fd, &info) == -1) {
        // Fallito reperimento informazioni sul file
        close(file_fd);
        return -1;
    }
    void *data = malloc(info.st_size);
    if(!data) {
        // Fallita allocazione buffer dati
        close(file_fd);
        return -1;
    }
    // Leggo i dati nel buffer
    if(readn(file_fd, data, info.st_size) != info.st_size) {
        // Fallita lettura o lettura incompleta
        close(file_fd);
        free(data);
        return -1;
    }
    close(file_fd); // Posso quindi chiudere il file

    // Dati letti: invio la richiesta comprendente nel terzo campo la dimensione del buffer
    // che sarà inviato al server
    struct request_t *request = newrequest(WRITE_FILE, 0, strlen(pathname) + 1, info.st_size);
    if(!request) {
        // errore di allocazione
        return -1;
    }
    // Scrivo la richiesta sul socket
    if(writen(csock, request, sizeof(*request)) != sizeof(*request)) {
        free(request);
        return -1;
    }
    // Scrivo il path del file da creare sul socket
    size_t slen = strlen(pathname) + 1;
    if(writen(csock, pathname, slen) != slen) {
        free(request);
        return -1;
    }
    // Scrivo i dati del file
    if(writen(csock, data, info.st_size) != info.st_size) {
        // Fallita scrittura o scrittura incompleta
        free(data);
        free(request);
        return -1;
    }
    free(request);

    // Attendo la risposta dal server
    struct reply_t *rep = malloc(sizeof(struct reply_t));
    if(!rep) {
        return -1;
    }
    if(readn(csock, rep, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
        // errore nella risposta
        free(rep);
        return -1;
    }
    if(rep->status == REPLY_NO) {
        // operazione negata
        free(rep);
        return -1;
    }

     // Operazione consentita
    // Controllo se ho avuto delle espulsioni di file
    if(rep->nbuffers > 0) {
        // leggo le dimensioni dei file ricevuti
        size_t *sizes = malloc(rep->nbuffers * sizeof(size_t));
        if(!sizes) {
            // fallita allocazione
            free(rep);
            return -1;
        }
        if(readn(csock, sizes, rep->nbuffers * sizeof(size_t)) != rep->nbuffers * sizeof(size_t)) {
            // fallita lettura
            free(sizes);
            free(rep);
            return -1;
        }

        // leggo i path dei file ricevuti (di cui conosco la lunghezza totale)
        char *paths = malloc(rep->paths_sz);
        if(!paths) {
            // errore di allocazione
            free(sizes);
            free(rep);
            return -1;
        }
        if(readn(csock, paths, rep->paths_sz) != rep->paths_sz) {
            // lettura path incompleta
            free(sizes);
            free(paths);
            free(rep);
            return -1;
        }

        int num_docs = -1; // write_swp restituisce il numero di file ricevuti e scritti in dirname
        num_docs = write_swp(csock, dirname, rep->nbuffers, sizes, paths);
        if(num_docs == -1) {
            // Qualcosa ha fallito
            free(sizes);
            free(paths);
            free(rep);
            return -1;
        }
        free(sizes);
        free(paths);
    } // Altrimenti nessun file è stato espulso

    // libero memoria
    free(rep);

    return 0;
}

// invia al server la richiesta di acquisire la mutua esclusione sul file pathname
int lockFile(const char *pathname) {
    // Verifico che il client sia connesso al server
    int csock = isConnected();
    if(csock == -1) {
        return -1;
    }

    // preparo la richiesta
    struct request_t *request = newrequest(LOCK_FILE, 0, strlen(pathname) + 1, 0);
    if(!request) {
        // errore di allocazione
        return -1;
    }
    // Alloco memoria per la risposta del server
    struct reply_t *rep = malloc(sizeof(struct reply_t));
    if(!rep) {
        free(request);
        return -1;
    }

    struct timespec delay;
    memset(&delay, 0, sizeof(delay));
    delay.tv_sec = 0;
    delay.tv_nsec = 100000; // delay di 100 millisecondi = 100000 nanosecondi

    // Flag per indicare che la mutua esclusione è stata acquisita
    int lock_failed = 1;
    do {
        // Scrivo la richiesta sul socket
        if(writen(csock, request, sizeof(struct request_t)) != sizeof(struct request_t)) {
            // errore di scrittura richiesta
            free(request);
            return -1;
        }
        // Scrivo il path del file su il client vuole acquisire mutua esclusione
        size_t slen = strlen(pathname) + 1;
        if(writen(csock, pathname, slen) != slen) {
            // errore di scrittura path
            free(request);
            return -1;
        }
        // Leggo la risposta del server
        if(readn(csock, rep, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
            // errore nella lettura della risposta
            free(rep);
            return -1;
        }
        if(rep->status == REPLY_YES) {
            // operazione consentita: il client ha la mutua esclusione su pathname
            lock_failed = 0; // quindi posso terminare l'operazione
            printf("lock acquisita su %s\n", pathname);
        }
        // Se la lock fallisce la api aspetta 100ms e poi tenta di nuovo di effettuare l'operazione
        else {
            printf("Fallita acquisizione lock su %s: riprovo tra 100ms\n", pathname);
            if(nanosleep(&delay, NULL) == -1) {
                // syscall interrotta da un segnale: lock fallisce
                int saved_errno = errno;
                free(request);
                free(rep);
                errno = saved_errno;
                return -1;
            }
        }
    } while(lock_failed);
    free(rep);
    free(request);

    // Operazione consentita
    return 0;
}

// invia al server la richiesta di rilasciare la mutua esclusione sul file pathname
int unlockFile(const char *pathname) {
    // Verifico che il client sia connesso al server
    int csock = isConnected();
    if(csock == -1) {
        return -1;
    }

    // preparo la richiesta
    struct request_t *request = newrequest(UNLOCK_FILE, 0, strlen(pathname) + 1, 0);
    if(!request) {
        // errore di allocazione
        return -1;
    }
    // Scrivo la richiesta sul socket
    if(writen(csock, request, sizeof(struct request_t)) != sizeof(struct request_t)) {
        free(request);
        return -1;
    }
    // Scrivo il path del file su cui rilasciare la lock sul socket
    size_t slen = strlen(pathname) + 1;
    if(writen(csock, pathname, slen) != slen) {
        free(request);
        return -1;
    }
    free(request);

    // Attendo la risposta dal server
    struct reply_t *rep = malloc(sizeof(struct reply_t));
    if(!rep) {
        return -1;
    }
    if(readn(csock, rep, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
        // errore nella risposta
        free(rep);
        return -1;
    }
    if(rep->status == REPLY_NO) {
        // operazione negata
        free(rep);
        return -1;
    }
    free(rep);

    // Operazione consentita
    return 0;
}

// invia al server la richiesta di rimozione del file dal server (solo se ha lock su tale file)
int removeFile(const char *pathname) {
    // Verifico che il client sia connesso al server
    int csock = isConnected();
    if(csock == -1) {
        return -1;
    }
    // preparo la richiesta
    struct request_t *request = newrequest(REMOVE_FILE, 0, strlen(pathname) + 1, 0);
    if(!request) {
        // errore di allocazione
        return -1;
    }
    // Scrivo la richiesta sul socket
    if(writen(csock, request, sizeof(struct request_t)) != sizeof(struct request_t)) {
        free(request);
        return -1;
    }
    // Scrivo il path del file da rimuovere
    size_t slen = strlen(pathname) + 1;
    if(writen(csock, pathname, slen) != slen) {
        free(request);
        return -1;
    }
    free(request);

    // Attendo la risposta dal server
    struct reply_t *rep = malloc(sizeof(struct reply_t));
    if(!rep) {
        return -1;
    }
    if(readn(csock, rep, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
        // errore nella risposta
        free(rep);
        return -1;
    }
    if(rep->status == REPLY_NO) {
        // operazione negata
        free(rep);
        return -1;
    }
    free(rep);

    // Operazione consentita
    return 0;
}
