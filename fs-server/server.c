// header server
#include <server-utils.h>
// header API
#include <fs-api.h>
// header utilità
#include <utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <sys/types.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h> // per select
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Effettua la rimozione del socket e liberazione della memoria occupata dalla struttura dati del server
void clean_server(struct serv_params *params, struct fs_ds_t *ds);
// Accetta la connessione e restituisce il socket da usare (o -1 in caso di errore)
int accept_connection(const int serv_sock);
// Legge dal socket del client (client_fd) la richiesta e la inserisce nella coda per servirla
int processRequest(struct fs_ds_t *server_ds, const int client_sock);
// Chiude tutti gli fd in set, rimuovendoli anche dal set
void close_fd(fd_set *set, const int maxfd);
// Stampa su stdout delle statistiche di utilizzo del server
void stats(struct serv_params *params, struct fs_ds_t *ds);

// Funzione main del server multithreaded: ricopre il ruolo di thread manager
int main(int argc, char **argv) {
    // Ignora SIGPIPE istallando l'apposito signal handler (process-wide)
    struct sigaction ign_pipe;
    memset(&ign_pipe, 0, sizeof(ign_pipe));
    ign_pipe.sa_handler = SIG_IGN;
    if(sigaction(SIGPIPE, &ign_pipe, NULL) == -1) {
        // errore nell'installazione signal handler per ignorare SIGPIPE
        // termino brutalmente il server, anche perché terminerebbe una volta che l'ultimo client si disconnette
        return 1;
    }

    // La struttura run_params verrà inizializzata con i parametri di configurazione del server
    struct serv_params run_params;
    memset(&run_params, 0, sizeof(struct serv_params)); // per sicurezza azzero tutto

    // Se è stata passata l'opzione -f al server leggo il file di configurazione da essa
    int parse_status;
    if(argc == 3 && strncmp(argv[1], "-f", strlen(argv[1])) == 0) {
        parse_status = parse_config(&run_params, argv[2]);
    }
    else {
        parse_status = parse_config(&run_params, NULL);
    }

    int errcode; // variabile usata per salvare errno e poi ritornare dal main con tale valore

    // errore di parsing: lo riporto su standard error ed esco con tale codice di errore
    if(parse_status == -1) {
        errcode = errno;
        fprintf(stderr, "Errore: Fallito parsing del file di configurazione \"%s\": %s\n", argv[1], strerror(errno));
        return errcode;
    }

    // parsing completato con successo: apro il file di log
    // in append e creandolo se non esiste
    int logfile_fd;
    if((logfile_fd = open(run_params.log_path, O_WRONLY|O_CREAT|O_TRUNC, PERMS_ALL_READ)) == -1) {
        // errore nell'apertura o nella creazione del file di log. L'errore è riportato
        // su standard error, ma il server continua l'esecuzione
        errcode = errno;
        fprintf(stderr, "Warning: Impossibile creare o aprire il file di log \"%s\": %s\n", run_params.log_path, strerror(errno));
        clean_server(&run_params, NULL);
        return errcode;
    }

    // inizializzo le strutture dati del server
    struct fs_ds_t *server_ds = NULL; // la struttura dati viene allocata in init_ds
    if(init_ds(&run_params, &server_ds) == -1) {
        errcode = errno;
        fprintf(stderr, "Errore: Fallita inizializzazione strutture dati del server: %s\n", strerror(errno));
        clean_server(&run_params, NULL);
        return errcode;
    }
    // assegno il descrittore del file di log alla struttura dati, consentendo
    // quindi il logging delle operazioni anche all'interno dei thread worker
    server_ds->log_fd = logfile_fd;

    // Creo il socket sul quale il server ascolta connessioni (e lo assegno)
    int listen_connections;
    if((listen_connections = sock_init(run_params.sock_path, strlen(run_params.sock_path))) == -1) {
        errcode = errno; // salvo l'errore originato dalla funzione sock_init per ritornarlo
        if(logging(server_ds, errno, "Errore: Impossibile creare socket per ascolto connessioni") == -1) {
            errno = errcode;
            fprintf(stderr, "Errore: Impossibile creare socket \"%s\": %s\n", run_params.sock_path, strerror(errno));
            clean_server(&run_params, server_ds);
        }
        return errcode;
    }
    //-------------------------------------------------------------------------------
    // Con la creazione del thread di terminazione il processo diventa multithreaded

    // Creo il thread che gestisce la terminazione
    pthread_t term_tid;
    if(pthread_create(&term_tid, NULL, term_thread, server_ds) == -1) {
        errcode = errno;
        if(logging(server_ds, errno, "Impossibile creare il thread di terminazione") == -1) {
            // in questo caso va bene usare strerror, nonostante sia MT-unsafe, poichè viene
            // chiamata se e solo se la creazione del thread fallisce, quindi il processo rimane single-threaded
            errno = errcode;
            fprintf(stderr, "Impossibile creare il thread di terminazione: %s\n", strerror(errno));
            clean_server(&run_params, server_ds);
        }
        return errcode;
    }

    // adesso disabilito tutti i segnali per il thread manager e per quelli da esso creati (worker)
    sigset_t mask_sigs;
    if(sigfillset(&mask_sigs) == -1) {
        errcode = errno;
        if(logging(server_ds, errno, "Manager thread: Impossibile creare maschera segnali") == -1) {
            errno = errcode;
            perror("Manager thread: Impossibile creare maschera segnali");
        }
        // cleanup e poi esco
        clean_server(&run_params, server_ds);
        return errcode;
    }
    if((errcode = pthread_sigmask(SIG_BLOCK, &mask_sigs, NULL)) != 0) {
    if(logging(server_ds, errcode, "Manager thread: Impossibile bloccare i segnali") == -1) {
            errno = errcode;
            perror("Manager thread: Impossibile bloccare i segnali");
        }
        // cleanup e poi esco
        clean_server(&run_params, server_ds);
        return errcode;
    }

    // Creo la thread pool
    pthread_t *workers = malloc(run_params.thread_pool * sizeof(pthread_t));
    if(!workers) {
    errcode = errno;
    if(logging(server_ds, errno, "Manager thread: Impossibile creare threadpool") == -1) {
            errno = errcode;
            perror("Manager thread: Impossibile creare threadpool");
        }
        clean_server(&run_params, server_ds);
        return errcode;
    }
    long int i;
    for(i = 0; i < run_params.thread_pool; i++) {
    // Il puntatore alle strutture dati viene passato ad ogni worker thread come parametro
    if((errcode = pthread_create(&(workers[i]), NULL, work, server_ds)) != 0) {
            if(logging(server_ds, errcode, "Manager thread: Impossibile creare worker thread") == -1) {
                errno = errcode;
                perror("Manager thread: Impossibile creare worker thread");
            }
            // mando il segnale di terminazione manualmente al thread di terminazione
            pthread_kill(term_tid, SIGINT);
            clean_server(&run_params, server_ds);
            return errcode;
        }
    }

    // Main loop: accetta connessioni dai client e manda le richieste ai worker pronti
    // Inizialmente ascolto solo su tre socket: quello per accettare connessioni ed i
    // descrittori di lettura delle pipe di feedback e di terminazione
    fd_set fd_read, fd_read_cpy;
    FD_ZERO(&fd_read);
    FD_SET(listen_connections, &fd_read);
    FD_SET(server_ds->feedback[0], &fd_read);
    FD_SET(server_ds->termination[0], &fd_read);
    // L'indice massimo sarà inizialmente il massimo tra i tre descrittori
    int fd;
    int max_fd_idx;
    if(listen_connections > server_ds->feedback[0]) {
        max_fd_idx = listen_connections;
    }
    if(server_ds->termination[0] > max_fd_idx) {
        max_fd_idx = server_ds->termination[0];
    }

    // Esco da questo while soltanto se uno dei segnali di terminazione viene
    // gestito dal thread che si occupa della terminazione
    while(1) {
        // setto i descrittori da ascoltare con select
        fd_read_cpy = fd_read;
        // Il server seleziona i file descriptor pronti in lettura
        if(select(max_fd_idx + 1, &fd_read_cpy, NULL, NULL, NULL) == -1) {
            // Select interrota da un errore non gestito: logging + terminazione
            if(errno != EINTR) {
                errcode = errno;
                if(logging(server_ds, errno, "Manager thread: Select fallita") == -1) {
                    errno = errcode;
                    perror("Manager thread: Select fallita");
                }
                // mando il segnale di terminazione manualmente al thread di terminazione
                pthread_kill(term_tid, SIGINT);
                // salto direttamente a term in modo da effettuare la join
                goto term;
            }
        }

        // max_fd_idx è aggiornato solo dopo il for, per cui devo tenere il nuovo valore in un temporaneo
        int new_maxfd = max_fd_idx;

        // Devo trovare i file descriptor pronti ed eseguire azioni di conseguenza
        for(fd = 0; fd < max_fd_idx + 1; fd++) {
            // Era pronto il descrittore il lettura dalla pipe di terminazione, per cui devo terminare il server
            if(fd == server_ds->termination[0] && FD_ISSET(fd, &fd_read_cpy)) {
                // Devo leggere dalla pipe per capire quale tipo di terminazione (veloce o lenta)
                int term;
                if(read(server_ds->termination[0], &term, sizeof(term)) == -1) {
                    perror("Manager thread: Impossibile leggere tipo di terminazione");
                    pthread_kill(pthread_self(), SIGKILL); // terminazione brutale
                }
                if(term == SLOW_TERM) {
                    // in caso di terminazione lenta chiudo solo il socket su cui si accettano connessioni
                    if(close(listen_connections) == -1) {
                        perror("Manager thread: Impossibile chiudere il socket del server");
                    }
                    // devono essere comunque servite le richieste dei client connessi, quindi tolgo
                    // solo listen_connections da quelli ascoltati dalla select
                    FD_CLR(listen_connections, &fd_read);
                    break; // altri socket pronti sono esaminati solo dopo aver aggiornato il set
                }
                // In caso di terminazione veloce chiudo tutti i descrittori
                // Viene effettuata di default anche se è stato letto un valore che non sia FAST_TERM dalla pipe
                else {
                    // Lascio aperti soltanti il descrittore per il feedback e per la terminazione
                    close_fd(&fd_read, max_fd_idx + 1);

                    if(pthread_mutex_lock(&(server_ds->mux_jobq)) == -1) {
                        if(logging(server_ds, errno, "Fallito lock coda di richieste") == -1) {
                            perror("Fallito lock coda di richieste");
                        }
                        return -1;
                    }

                    // svuoto la coda di richieste
                    struct node_t *n = NULL;
                    while((n = pop(server_ds->job_queue)) != NULL) {
                        free(n->data);
                        free(n);
                    }
                    // Inserisco tante richieste di terminazione veloce quanti sono i worker
                    struct request_t *req = newrequest(FAST_TERM, 0, 0, 0);

                    for(size_t n = 0; n < run_params.thread_pool; n++) {
                        if(enqueue(server_ds->job_queue, req, sizeof(struct request_t), -1) == -1) {
                            if(logging(server_ds, errno, "Fallito inserimento nella coda di richieste") == -1) {
                                perror("Fallito inserimento nella coda di richieste");
                            }
                            return -1;
                        }
                    }
                    free(req);
                    // Quindi segnalo ai thread worker che vi è una nuova richiesta
                    pthread_cond_broadcast(&(server_ds->new_job));

                    if(pthread_mutex_unlock(&(server_ds->mux_jobq)) == -1) {
                        if(logging(server_ds, errno, "Fallito unlock coda di richieste") == -1) {
                            perror("Fallito unlock coda di richieste");
                        }
                        return -1;
                    }

                    goto term; // salto fuori dal for e dal while
                }
            }
            // Se ci sono connessioni in attesa le accetta
            if(fd == listen_connections && FD_ISSET(fd, &fd_read_cpy)) {
                int client_sock;
                if((client_sock = accept_connection(listen_connections)) == -1) {
                    errcode = errno;
                    // errore nella connessione al client, ma non fatale: il server va avanti normalmente
                    // tuttavia eseguo il logging dell'operazione
                    if(logging(server_ds, errno, "Manager thread: Impossibile accettare connessione di un client") == -1) {
                        errno = errcode;
                        perror("Manager thread: Impossibile accettare connessione di un client");
                    }
                    continue; // posso passare ad esaminare il prossimo socket pronto
                }
                // Se è stata accettata la connessione allora si può aggiungere il socket
                // assegnato al set di quelli ascoltati da select ed aggiungere il client
                // all'array di clients connessi
                if(add_connection(server_ds, client_sock) == -1) {
                    // impossibile aggiungere un client: chiudo il socket della connessione
                    close(client_sock);
                }
                else {
                    FD_SET(client_sock, &fd_read);
                    // Se necessario devo aggiornare il massimo indice dei socket ascoltati
                    if(new_maxfd < client_sock) {
                        new_maxfd = client_sock;
                    }
                }
            }
            // Se ho ricevuto feeback da un worker
            else if(fd == server_ds->feedback[0] && FD_ISSET(fd, &fd_read_cpy)) {
                // leggo il socket servito dalla pipe
                int sock = -1;
                if(read(server_ds->feedback[0], (void*)&sock, sizeof(int)) == -1) {
                    // se la read fallisce lo notifico, ma setto comunque il socket
                    errcode = errno;
                    if(logging(server_ds, errno, "Manager thread: Impossibile leggere feedback") == -1) {
                        errno = errcode;
                        perror("Manager thread: Impossibile leggere feedback");
                    }
                }

                // Se il socket è un numero negativo diverso da -1 allora è un socket chiuso
                // Perciò non devo reimmetterlo nel set, altrimenti rimane fuori
                if(sock > 0) {
                    // Rimetto il client tra quelli che il server ascolta (cioè in fd_read)
                    FD_SET(sock, &fd_read);
                    // Se necessario devo aggiornare il massimo indice dei socket ascoltati
                    if(new_maxfd < sock) {
                        new_maxfd = sock;
                    }
                }
                // Verifico se il client che si è disconnesso era l'ultimo rimasto dopo un SIGHUP ricevuto
                else {
                    if(sock < 0 && server_ds->slow_term == 1 && server_ds->connected_clients == 0) {
                        // In tal caso i worker sono terminati e posso saltare alla join
                        goto term;
                    }
                }
                break; // valuto di nuovo i socket pronti dopo aver ascoltato sock
            }
            // Se ho ricevuto una richiesta da un client devo inserirla nella coda di richieste
            else if(FD_ISSET(fd, &fd_read_cpy)) {
                if(processRequest(server_ds, fd) == -1) {
                    errcode = errno;
                    if(logging(server_ds, errno, "Manager thread: Fallito processing richiesta") == -1) {
                        errno = errcode;
                        perror("Manager thread: Fallito processing richiesta");
                    }
                    // errore nel processing della richiesta
                    struct reply_t *reply = NULL;
                    if((reply = newreply(REPLY_NO, 0, NULL)) == NULL) {
                        // errore allocazione risposta
                        errcode = errno;
                        if(logging(server_ds, errno, "Manager thread: Fallita allocazione risposta") == -1) {
                            errno = errcode;
                            perror("Manager thread: Fallita allocazione risposta");
                        }
                    }
                    else if(writen(fd, reply, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
                        errcode = errno;
                        if(logging(server_ds, errno, "Manager thread: Fallita scrittura risposta") == -1) {
                            errno = errcode;
                            perror("Manager thread: Fallita scrittura risposta");
                        }
                        free(reply);
                    }
                }

                // Poi tolgo il fd da quelli ascoltati in attesa che qualche worker
                // completi la richiesta (lo stesso client non può mandare nuove richieste finché questa non è stata trattata)
                FD_CLR(fd, &fd_read);
            }
        }

        // Aggiorno il massimo fd da controllare nella select
        max_fd_idx = new_maxfd;
    }

term:
    printf("[SERVER] Terminazione %s\n", (server_ds->slow_term == 1 ? "lenta" : "veloce"));
    // Risveglio tutti i thread sospesi sulla variabile di condizione della coda di richieste
    pthread_cond_broadcast(&(server_ds->new_job));
    // Aspetto la terminazione dei worker thread (terminano da soli, ma necessario per deallocare risorse)
    for(i = 0; i < run_params.thread_pool; i++) {
        if((errcode = pthread_join(workers[i], NULL)) != 0) {
            if(logging(server_ds, errcode, "Impossibile effettuare il join dei thread") == -1) {
                errno = errcode;
                perror("Impossibile effettuare il join dei thread");
            }
            free(workers);
            stats(&run_params, server_ds);
            clean_server(&run_params, server_ds);
            return errcode;
        }
    }
    // libero memoria
    free(workers);
    if((errcode = pthread_join(term_tid, NULL)) != 0) {
        if(logging(server_ds, errcode, "Impossibile effettuare il join dei thread") == -1) {
            errno = errcode;
            perror("Impossibile effettuare il join dei thread");
        }
        stats(&run_params, server_ds);
        clean_server(&run_params, server_ds);
        return errcode;
    }

    // Stampo su stdout statistiche
    stats(&run_params, server_ds);
    // libero strutture dati del server
    clean_server(&run_params, server_ds);

    return 0;
}

void clean_server(struct serv_params *params, struct fs_ds_t *ds) {
    // rimuovo il socket dal filesystem
    if(params) {
        if(unlink(params->sock_path) == -1) {
            if(logging(ds, errno, "Fallita rimozione socket") == -1) {
                perror("Fallita rimozione socket");
            }
        }
        // devo liberare anche la stringa contenente il path del file di log e del socket
        free(params->sock_path);
        free(params->log_path);
    }
    // libero le strutture dati del server
    if(ds) {
        free_serv_ds(ds);
    }
}

// Accetta la connessione e restituisce il socket da usare
int accept_connection(const int serv_sock) {
    int newsock;
    if((newsock = accept(serv_sock, NULL, NULL)) == -1) {
        return -1;
    }
    return newsock;
}

int processRequest(struct fs_ds_t *server_ds, const int client_sock) {
    // leggo richiesta
    struct request_t *req = malloc(sizeof(struct request_t));
    if(!req) {
        return -1;
    }
    if(readn(client_sock, req, sizeof(struct request_t)) != sizeof(struct request_t)) {
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

    if(enqueue(server_ds->job_queue, req, sizeof(struct request_t), client_sock) == -1) {
        if(logging(server_ds, errno, "Fallito inserimento nella coda di richieste") == -1) {
            perror("Fallito inserimento nella coda di richieste");
        }
        return -1;
    }
    free(req);

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

void close_fd(fd_set *set, const int maxfd) {
    // chiudo tutti i fd su cui opera la select tranne quelli di feedback e di terminazione
    int i;
    for(i = 0; i <= maxfd; i++) {
        if(FD_ISSET(i, set)) {
            if(close(i) == -1) {
                fprintf(stderr, "Impossibile chiudere il fd %d\n", i);
            }
        }
    }
    FD_ZERO(set);
}

// Stampa su stdout delle statistiche di utilizzo del server
void stats(struct serv_params *params, struct fs_ds_t *ds) {
    puts("======= RIEPILOGO DELL\'ESECUZIONE =======");
    printf("Socket del server: %s\n", params->sock_path);
    printf("File di log: %s\n", params->log_path);
    printf("Client connessi al momento della terminazione: %lu\n", ds->connected_clients);
    puts("======= MEMORIA =======");
    printf("Massima quantità di memoria %lu Mbyte (%lu byte)\n", ds->max_mem/1048576, ds->max_mem);
    printf("Memoria in uso al momento della terminazione: %lu Mbyte (%lu byte)\n", ds->curr_mem/1048576, ds->curr_mem);
    printf("Massima quantità di memoria occupata : %lu Mbyte (%lu byte)\n", ds->max_used_mem/1048576, ds->max_used_mem);
    printf("Chiamate algoritmo di rimpiazzamento FIFO: %lu\n", ds->cache_triggered);
    puts("======= FILES =======");
    printf("Massimo numero di file %lu\n", ds->max_files);
    printf("File aperti al momento della terminazione: %lu\n", ds->curr_files);
    printf("Massimo numero di file aperti durante l'esecuzione: %lu\n", ds->max_nfiles);
    printf("File presenti nel server alla terminazione (ordinati dal meno recente al più recente):\n");
    struct node_t *path = ds->cache_q->head;
    struct fs_filedata_t *file = NULL;
    while(path) {
        file = find_file(ds, (char*)path->data);
        printf("\"%s\"", (char*)path->data);
        if(file) {
            printf(" (%lu bytes)\n", file->size);
        }
        else {
            printf(": Impossibile recuperare il file (inconsistenza tra hashtable e coda di file)\n");
        }
        path = path->next;
    }
    puts("===============");
}
