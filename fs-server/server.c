// header progetto
#include <utils.h>
#include <server-utils.h>
#include <fs-api.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h> // for select
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

// REMINDER: il file di configurazione alternativo (fs-config) è nella home directory

// Accetta la connessione e restituisce il socket da usare (o -1 in caso di errore)
int accept_connection(const int serv_sock);
// Legge dal socket del client (client_fd) la richiesta e la inserisce nella coda per servirla
int processRequest(struct fs_ds_t *server_ds, const int client_fd);

int should_term(struct term_params_t *tp, fd_set *select_set, const int maxsock, const int serv_sock);

// Funzione main del server multithreaded: effettua il ruolo di manager thread
int main(int argc, char **argv) {
	// ignoro SIGPIPE (process-wide)
    struct sigaction ign_pipe;
    memset(&ign_pipe, 0, sizeof(ign_pipe));
    ign_pipe.sa_handler = SIG_IGN;
    if(sigaction(SIGPIPE, &ign_pipe, NULL) == -1) {
        // errore nell'installazione signal handler per ignorare SIGPIPE
        // termino brutalmente il server, anche perché terminerebbe una volta che l'ultimo client si disconnette
        pthread_kill(pthread_self(), SIGKILL);
    }

    // effettua il parsing del file di configurazione riempiendo i campi della struttura
    struct serv_params run_params;
    memset(&run_params, 0, sizeof(struct serv_params)); // per sicurezza azzero tutto

    // se ho specificato l'opzione -f al server leggo il file di configurazione da essa
    int parse_status;
    if(argc == 3 && strncmp(argv[1], "-f", strlen(argv[1])) == 0) {
        parse_status = parse_config(&run_params, argv[2]);
    }
    else {
        parse_status = parse_config(&run_params, NULL);
    }

    // errore di parsing: lo riporto su standard output ed esco con tale codice di errore
    if(parse_status == -1) {
        int errcode = errno;
        perror("Fallito il parsing del file di configurazione");
        return errcode;
    }

    // stampo tutti i parametri del server
    printf("thread pool size: %ld\n", run_params.thread_pool);
    printf("max memory size: %ldMbyte\n", run_params.max_memsz);
    printf("max file count: %ld\n", run_params.max_fcount);
    printf("socket path: %s\n", run_params.sock_path);
    printf("log file path: %s\n", run_params.log_path);

    int logfile_fd;
    // parsing completato con successo: apro il file di log
    // in scrittura (append), creandolo se non esiste
    if((logfile_fd = open(run_params.log_path, O_WRONLY|O_CREAT|O_APPEND, PERMS_ALL_READ)) == -1) {
        // errore nell'apertura o nella creazione del file di log. L'errore è riportato
        // su standard output, ma il server continua l'esecuzione
        perror("Impossibile creare o aprire il file di log");
        return 1;
    }

    // inizializzo le strutture dati del server
    struct fs_ds_t *server_ds = NULL; // la struttura dati viene allocata in init_ds
    if(init_ds(&run_params, &server_ds) == -1) {
        perror("Impossibile inizializzare strutture dati server");
        return 1;
    }
    // assegno il descrittore del file di log alla struttura dati, per effettuare il logging
    // anche nei thread worker
    server_ds->log_fd = logfile_fd;

    // Creo il socket sul quale il server ascolta connessioni (e lo assegno)
    int listen_connections;
    if((listen_connections = sock_init(run_params.sock_path, strlen(run_params.sock_path))) == -1) {
        if(log(server_ds, errno, "Impossibile creare socket per ascolto connessioni") == -1) {
            perror("Impossibile creare socket per ascolto connessioni");
        }
        return 1;
    }

    // attributi per il thread di terminazione
    pthread_attr_t attrs;
    if(pthread_attr_init(&attrs) != 0 && pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED != 0)) {
        if(log(server_ds, errno, "Impossibile settare thread detached") == -1) {
            perror("Impossibile settare thread detached");
        }
        return 1;
    }
    // Inizializzo la struttura dati che regola la terminazione
    struct term_params_t term_params;
    // inizializzo mutex da usare per controllare terminazione
    pthread_mutex_init(&(term_params.mux_term), NULL);
    term_params.fast_term = 0;
    term_params.slow_term = 0;

    //-------------------------------------------------------------------------------
    // Da qui in poi multithreaded

	// creo il thread che gestisce la terminazione
    pthread_t term_tid;
    if(pthread_create(&term_tid, &attrs, term_thread, &term_params) == -1) {
        if(log(server_ds, errno, "Impossibile creare il thread di terminazione") == -1) {
            perror("Impossibile creare il thread di terminazione");
        }
        return 2;
    }

    // adesso disabilito tutti i segnali per il thread manager e per quelli da esso creati (worker)
    sigset_t mask_sigs;
    if(	sigemptyset(&mask_sigs) == -1
            || sigaddset(&mask_sigs, SIGHUP) == -1
            || sigaddset(&mask_sigs, SIGQUIT) == -1
            || sigaddset(&mask_sigs, SIGINT) == -1) {
        if(log(server_ds, errno, "Impossibile creare maschera segnali") == -1) {
            perror("Impossibile creare maschera segnali");
        }
        return 1;
    }
    int ret;
    if((ret = pthread_sigmask(SIG_BLOCK, &mask_sigs, NULL)) != 0) {
        if(log(server_ds, ret, "Impossibile bloccare i segnali") == -1) {
            perror("Impossibile bloccare i segnali");
        }
        return 1;
    }

    // creo la thread pool
    pthread_t *workers = malloc(run_params.thread_pool * sizeof(pthread_t));
    if(!workers) {
        if(log(server_ds, errno, "Impossibile creare la threadpool") == -1) {
            perror("Impossibile creare thread pool");
        }
        return 1;
    }

    long int i;
    for(i = 0; i < run_params.thread_pool; i++) {
        // Il puntatore alle strutture dati viene passato ad ogni worker thread come parametro
        if(pthread_create(&workers[i], NULL, work, server_ds) != 0) {
            if(log(server_ds, errno, "Impossibile creare la threadpool") == -1) {
                perror("Impossibile creare thread pool");
            }
            return 2;
        }
    }

    // Main loop: accetto connessioni dai client e mando le richieste ai worker pronti
    // Inizialmente ascolto solo sul socket per accettare connessioni e il read end della pipe di feedback
    fd_set fd_read, fd_read_cpy;
    FD_ZERO(&fd_read);
    FD_SET(listen_connections, &fd_read);
    FD_SET(server_ds->feedback[0], &fd_read);
    // Devo mettere l'indice max che è il max tra i due
    int max_fd_idx = (listen_connections > server_ds->feedback[0] ? listen_connections : server_ds->feedback[0]);
    int fd;

    // Questo è il loop del thread manager, che accetta connessioni ed inserisce nella coda
    // le richieste per i thread worker, ricevendo poi il feedback
    while(1) {
        if(should_term(&term_params, &fd_read, max_fd_idx, listen_connections)) {
            break;
        }

        fd_read_cpy = fd_read;
        // Il server seleziona i file descriptor pronti
        if(select(max_fd_idx + 1, &fd_read_cpy, NULL, NULL, NULL) == -1) {
            if(errno == EINTR) {
                // era stata interrotta da un segnale gestito (terminazione)
                if(should_term(&term_params, &fd_read, max_fd_idx, listen_connections)) {
                    break;
                }
                else {
                    fd_read_cpy = fd_read; // potrebbe aver modificato fd_read togliendo listen_connections
                }
            }
            // un altro errore della select => provoca terminazione
            else {
                if(log(server_ds, errno, "Select fallita") == -1) {
                    perror("Select fallita");
                    return 2;
                }
            }
        }

        // max_fd_idx lo aggiorno solo dopo il for, per cui devo tenere il nuovo valore in un temporaneo
        int new_maxfd = max_fd_idx;
        // Devo trovare i file descriptor pronti
        for(fd = 0; fd < max_fd_idx + 1; fd++) {
            // Se ci sono connessioni in attesa le accetta
            if(fd == listen_connections && FD_ISSET(fd, &fd_read_cpy)) {
                int client_sock;
                if((client_sock = accept_connection(listen_connections)) == -1) {
                    // errore nella connessione al client, ma non fatale: il server va avanti normalmente
                    // tuttavia eseguo il logging dell'operazione
                    if(log(server_ds, errno, "Impossibile accettare connessione") == -1) {
                        perror("Impossibile accettare connessione");
                    }
                    continue;
                }
                // Invece, se è stata accettata, allora si può aggiungere
                // il socket del client a quelli ascoltati da select
                FD_SET(client_sock, &fd_read);
                // Se necessario devo aggiornare il massimo indice dei socket
                if(new_maxfd < client_sock) {
                    new_maxfd = client_sock;
                }
            }
            // Se ho ricevuto feeback da un worker
            else if(fd == server_ds->feedback[0] && FD_ISSET(fd, &fd_read_cpy)) {
                // leggo il socket servito
                int sock = -1;
                if(read(server_ds->feedback[0], (void*)&sock, sizeof(int)) == -1) {
                    if(log(server_ds, errno, "Impossibile leggere feedback") == -1) {
                        perror("Impossibile leggere feedback");
                    }
                }
                log(server_ds, errno, "Servita richiesta del client");
                // Rimetto il client tra quelli che il server ascolta (cioè in fd_read)
                FD_SET(sock, &fd_read);
                // Se necessario devo aggiornare il massimo indice dei socket
                if(new_maxfd < sock) {
                    new_maxfd = sock;
                }
            }
            // Se ho ricevuto una richiesta da un client devo inserirla nella coda di richieste
            else if(FD_ISSET(fd, &fd_read_cpy)) {
                if(processRequest(server_ds, fd) == -1) {
                    // errore nel processing della richiesta
                    struct reply_t *reply = NULL;
                    if((reply = newreply('N', 0)) == NULL) {
                        // errore allocazione risposta
                        puts("errore alloc risposta"); // TODO: log
                    }
                    writen(fd, reply, sizeof(struct reply_t));
                }
                // Poi tolgo il fd da quelli ascoltati
                FD_CLR(fd, &fd_read);
            }
        }

        // Aggiorno il massimo fd da controllare nella select
        max_fd_idx = new_maxfd;
    }

    for(i = 0; i < run_params.thread_pool; i++) {
        // Il puntatore alle strutture dati viene passato ad ogni worker thread come parametro
        if(pthread_join(workers[i], NULL) != 0) {
            if(log(server_ds, errno, "Impossibile joinare thread") == -1) {
                perror("Impossibile joinare thread");
            }
            return 2;
        }
    }

    unlink(run_params.sock_path);
    close(listen_connections);

    return 0;
}

// Accetta la connessione e restituisce il socket da usare
int accept_connection(const int serv_sock) {
    int newsock;
    if((newsock = accept(serv_sock, NULL, NULL)) == -1) {
        return -1;
    }
    return newsock;
}

int processRequest(struct fs_ds_t *server_ds, const int client_fd) {
    // leggo richiesta
    struct request_t *req = malloc(sizeof(struct request_t));
    if(!req) {
        return -1;
    }
    if(readn(client_fd, req, sizeof(struct request_t)) != sizeof(struct request_t)) {
        if(errno != EINTR) {
           return -1;
        }
    }

    // Ora inserisco la richiesta nella coda (in ME)
    if(pthread_mutex_lock(&(server_ds->mux_jobq)) == -1) {
        if(log(server_ds, errno, "Fallito lock coda di richieste") == -1) {
            perror("Fallito lock coda di richieste");
        }
        return -1;
    }

    if(enqueue(server_ds->job_queue, req, sizeof(struct request_t), client_fd) == -1) {
        if(log(server_ds, errno, "Fallito inserimento nella coda di richieste") == -1) {
            perror("Fallito inserimento nella coda di richieste");
        }
        return -1;
    }
    // Quindi segnalo ai thread worker che vi è una nuova richiesta
    pthread_cond_signal(&(server_ds->new_job));

    if(pthread_mutex_unlock(&(server_ds->mux_jobq)) == -1) {
        if(log(server_ds, errno, "Fallito unlock coda di richieste") == -1) {
            perror("Fallito unlock coda di richieste");
        }
        return -1;
    }

    return 0;
}

int should_term(struct term_params_t *tp, fd_set *select_set, const int maxsock, const int serv_sock) {
    // Prendo ME sulla struttura dati per la terminazione del server
    if(pthread_mutex_lock(&(tp->mux_term)) == -1) {
        perror("Fallita acquisizione ME su terminazione");
        return 0;
    }

    int i;
    // terminazione veloce: devono essere chiuse le connessioni esistenti
    if(tp->fast_term) {
        // chiudo tutti i socket su cui opera la select
        for(i = 0; i <= maxsock; i++) {
            if(FD_ISSET(i, select_set)) {
                if(close(i) == -1) {
                    perror("Impossibile chiudere il socket");
                }
            }
        }
        return 1; // solo in caso di fast_term esco immediatamente dal loop del server
    }
    // terminazione lenta: le connessioni esistenti rimangono aperte
    // fino alla loro chiusura da parte del client
    else if(tp->slow_term) {
        // devo chiudere immediatamente solo il socket su cui il server accetta connessioni (serv_sock)
        if(close(serv_sock) == -1) {
            perror("Impossibile chiudere il socket del server");
        }
        // devono essere comunque servite le richieste dei client connessi, quindi tolgo
        // solo serv_sock da quelli ascoltati dalla select
        FD_CLR(serv_sock, select_set);
    }

    if(pthread_mutex_unlock(&(tp->mux_term)) == -1) {
        perror("Fallito rilascio ME su terminazione");
        return 0;
    }

    return 0;
}
