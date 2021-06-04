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

// Accetta la connessione e restituisce il socket da usare (o -1 in caso di errore)
int accept_connection(const int serv_sock);
// Legge dal socket del client (client_fd) la richiesta e la inserisce nella coda per servirla
int processRequest(struct fs_ds_t *server_ds, const int client_fd);
// Chiude tutti gli fd in set, tranne feedpipe e termpipe, rimuovendoli anche dal set
void close_most_fd(fd_set *set, const int feedpipe, const int termpipe, const int maxsock);
// Stampa su stdout delle statistiche di utilizzo del server
void stats(struct fs_ds_t *ds);

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
        if(logging(server_ds, errno, "Impossibile creare socket per ascolto connessioni") == -1) {
            perror("Impossibile creare socket per ascolto connessioni");
        }
        return 1;
    }

    // attributi per il thread di terminazione
    pthread_attr_t attrs;
    if(pthread_attr_init(&attrs) != 0 && pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED != 0)) {
        if(logging(server_ds, errno, "Impossibile settare thread detached") == -1) {
            perror("Impossibile settare thread detached");
        }
        return 1;
    }

    //-------------------------------------------------------------------------------
    // Da qui in poi multithreaded

    // creo il thread che gestisce la terminazione
    pthread_t term_tid;
    if(pthread_create(&term_tid, &attrs, term_thread, server_ds) == -1) {
        if(logging(server_ds, errno, "Impossibile creare il thread di terminazione") == -1) {
            perror("Impossibile creare il thread di terminazione");
        }
        return 2;
    }

    // adesso disabilito tutti i segnali per il thread manager e per quelli da esso creati (worker)
    sigset_t mask_sigs;
    if(sigfillset(&mask_sigs) == -1) {
        if(logging(server_ds, errno, "Impossibile mascherare segnali (altri thread)") == -1) {
            perror("Impossibile mascherare segnali (altri thread)");
        }
        return 1;
    }
    int ret;
    if((ret = pthread_sigmask(SIG_BLOCK, &mask_sigs, NULL)) != 0) {
        if(logging(server_ds, ret, "Impossibile bloccare i segnali") == -1) {
            perror("Impossibile bloccare i segnali");
        }
        return 1;
    }

    // creo la thread pool
    pthread_t *workers = malloc(run_params.thread_pool*sizeof(pthread_t));
    if(!workers) {
        if(logging(server_ds, errno, "Impossibile creare la threadpool") == -1) {
            perror("Impossibile creare thread pool");
        }
        return 2;
    }
    long int i;
    for(i = 0; i < run_params.thread_pool; i++) {
        // Il puntatore alle strutture dati e per la terminazione viene passato ad ogni worker thread come parametro
        if(pthread_create(&(workers[i]), &attrs, work, server_ds) != 0) {
            if(logging(server_ds, errno, "Impossibile creare la threadpool") == -1) {
                perror("Impossibile creare thread pool");
            }
            return 2;
        }
    }

    // Main loop: accetto connessioni dai client e mando le richieste ai worker pronti
    // Inizialmente ascolto solo su tre socket: quello per accettare connessioni, il
    // read end della pipe di feedback e di terminazione
    fd_set fd_read, fd_read_cpy;
    FD_ZERO(&fd_read);
    FD_SET(listen_connections, &fd_read);
    FD_SET(server_ds->feedback[0], &fd_read);
    FD_SET(server_ds->termination[0], &fd_read);
    // Devo mettere l'indice max che è il max tra i tre
    int fd;
    int max_fd_idx;
    if(listen_connections > server_ds->feedback[0]) {
        max_fd_idx = listen_connections;
    }
    if(server_ds->termination[0] > max_fd_idx) {
        max_fd_idx = server_ds->termination[0];
    }

    // Questo è il loop del thread manager, che accetta connessioni ed inserisce nella coda
    // le richieste per i thread worker, ricevendo poi il feedback
    while(1) {
        fd_read_cpy = fd_read;
        // Il server seleziona i file descriptor pronti
        if(select(max_fd_idx + 1, &fd_read_cpy, NULL, NULL, NULL) == -1) {
            if(errno == EINTR) {
                // era stata interrotta da un segnale gestito (terminazione)
                perror("Select interrotta");
            }
            // un altro errore della select => provoca terminazione
            else {
                if(logging(server_ds, errno, "Select fallita") == -1) {
                    perror("Select fallita");
                    return 2;
                }
            }
        }

        // max_fd_idx lo aggiorno solo dopo il for, per cui devo tenere il nuovo valore in un temporaneo
        int new_maxfd = max_fd_idx;
        // Devo trovare i file descriptor pronti
        for(fd = 0; fd < max_fd_idx + 1; fd++) {
            if(fd == server_ds->termination[0] && FD_ISSET(fd, &fd_read_cpy)) {
                // il server ha ricevuto un segnale che ne provoca la terminazione
                // devo leggere dalla pipe per capire se è veloce o lenta
                int term;
                if(read(server_ds->termination[0], &term, sizeof(term)) == -1) {
                    perror("Impossibile leggere tipo di terminazione");
                    exit(1);
                }
                if(term == 1) {
                    // in caso di terminazione veloce chiudo tutti i socket
                    close_most_fd(&fd_read, server_ds->feedback[0], server_ds->termination[0], max_fd_idx + 1);

                    // Prendo ME sulla coda
                    if(pthread_mutex_lock(&(server_ds->mux_jobq)) == -1) {
                        // Fallita operazione di lock
                        return 1;
                    }
                    // sostisuisco la coda di richieste con una vuota, poi libero quella originale
                    struct Queue *qcpy = server_ds->job_queue;
                    server_ds->job_queue = NULL; // pop su questa coda restituirà NULL
                    free_Queue(qcpy);

                    if(pthread_mutex_unlock(&(server_ds->mux_jobq)) == -1) {
                        // Fallita operazione di unlock
                        return 1;
                    }
                    goto fast_term; // salto fuori dal for e dal while
                }
                else if(term == 2) {
                    // in caso di terminazione lenta chiudo solo il socket su cui si accettano connessioni
                    if(close(listen_connections) == -1) {
                        perror("Impossibile chiudere il socket del server");
                    }
                    // devono essere comunque servite le richieste dei client connessi, quindi tolgo
                    // solo serv_sock da quelli ascoltati dalla select
                    FD_CLR(listen_connections, &fd_read);
                    break; // altri socket sono esaminati solo dopo aver aggiornato il set
                }
                else {
                    printf("[SERVER] term = %c\n", term);
                }
            }
            // Se ci sono connessioni in attesa le accetta
            if(fd == listen_connections && FD_ISSET(fd, &fd_read_cpy)) {
                int client_sock;
                if((client_sock = accept_connection(listen_connections)) == -1) {
                    // errore nella connessione al client, ma non fatale: il server va avanti normalmente
                    // tuttavia eseguo il logging dell'operazione
                    if(logging(server_ds, errno, "Impossibile accettare connessione") == -1) {
                        perror("Impossibile accettare connessione");
                    }
                    continue;
                }
                // Invece, se è stata accettata, allora si può aggiungere

                // Devo espandere l'array di client connessi
                if(add_connection(server_ds, client_sock) == -1) {
                    // impossibile aggiungere un client
                    close(client_sock);
                }
                else {
                    // il socket del client a quelli ascoltati da select
                    FD_SET(client_sock, &fd_read);
                    // Se necessario devo aggiornare il massimo indice dei socket
                    if(new_maxfd < client_sock) {
                        new_maxfd = client_sock;
                    }
                }
            }
            // Se ho ricevuto feeback da un worker
            else if(fd == server_ds->feedback[0] && FD_ISSET(fd, &fd_read_cpy)) {
                // leggo il socket servito
                int sock = -1;
                if(read(server_ds->feedback[0], (void*)&sock, sizeof(int)) == -1) {
                    if(logging(server_ds, errno, "Impossibile leggere feedback") == -1) {
                        perror("Impossibile leggere feedback");
                    }
                }
                logging(server_ds, errno, "Servita richiesta del client");
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
                    if((reply = newreply('N', 0, NULL, NULL)) == NULL) {
                        // errore allocazione risposta
                        puts("errore alloc risposta"); // TODO: log
                    }
                    writen(fd, reply, sizeof(struct reply_t));
                    free(reply);
                }

                // Poi tolgo il fd da quelli ascoltati
                FD_CLR(fd, &fd_read);
            }
        }

        // Aggiorno il massimo fd da controllare nella select
        max_fd_idx = new_maxfd;
    }

fast_term:
    printf("fast term...\n");
    // Risveglio tutti i thread sospesi sulla variabile di condizione della coda di richieste
    pthread_cond_broadcast(&(server_ds->new_job));
    // Aspetto la terminazione dei worker thread (terminano da soli, ma necessario per deallocare risorse)
    //for(i = 0; i < run_params.thread_pool; i++) {
    //if(pthread_join(workers[i], NULL) != 0) {
    //if(logging(server_ds, errno, "Impossibile effettuare il join dei thread") == -1) {
    //perror("Impossibile effettuare il join dei thread");
    //}
    //return 2;
    //}
    //}
    free(workers);

    // Stampo su stdout statistiche
    stats(server_ds);

    // rimuovo il socket dal filesystem
    if(unlink(run_params.sock_path) == -1) {
        if(logging(server_ds, errno, "Fallita rimozione socket") == -1) {
            perror("Fallita rimozione socket");
        }
    }
    // libero le strutture dati del server
    free_serv_ds(server_ds);
    // devo liberare anche la stringa contenente il path del file di log e del socket
    free(run_params.sock_path);
    free(run_params.log_path);

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
        if(logging(server_ds, errno, "Fallito lock coda di richieste") == -1) {
            perror("Fallito lock coda di richieste");
        }
        return -1;
    }

    if(enqueue(server_ds->job_queue, req, sizeof(struct request_t), client_fd) == -1) {
        if(logging(server_ds, errno, "Fallito inserimento nella coda di richieste") == -1) {
            perror("Fallito inserimento nella coda di richieste");
        }
        return -1;
    }
    // Quindi segnalo ai thread worker che vi è una nuova richiesta
    pthread_cond_signal(&(server_ds->new_job));

    if(pthread_mutex_unlock(&(server_ds->mux_jobq)) == -1) {
        if(logging(server_ds, errno, "Fallito unlock coda di richieste") == -1) {
            perror("Fallito unlock coda di richieste");
        }
        return -1;
    }

    return 0;
}

void close_most_fd(fd_set *set, const int feedpipe, const int termpipe, const int maxsock) {
    // chiudo tutti i fd su cui opera la select tranne quelli di feedback e di terminazione
    int i;
    for(i = 0; i <= maxsock; i++) {
        if(!(i == feedpipe || i == termpipe) && FD_ISSET(i, set)) {
            if(close(i) == -1) {
                perror("Impossibile chiudere il fd");
            }
            FD_CLR(i, set);
        }
    }
}

// Stampa su stdout delle statistiche di utilizzo del server
void stats(struct fs_ds_t *ds) {
    printf("Massimo numero di file aperti: %lu\n", ds->max_nfiles);
    printf("Massima quantità di memoria occupata : %lu Mbyte (%lu byte)\n", ds->max_used_mem/1048576, ds->max_used_mem);
    printf("Chiamate algoritmo di rimpiazzamento FIFO: %lu\n", ds->cache_triggered);
    printf("Client connessi al momento della terminazione: %lu\n", ds->connected_clients);
    printf("File presenti nel server alla terminazione: ");
    struct node_t *file = ds->cache_q->head;
    while(file) {
        printf("\"%s\", ", (char*)file->data);
        file = file->next;
    }
    putchar('\n');
}
