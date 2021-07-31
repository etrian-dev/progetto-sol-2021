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
    // letti da un file di configurazione
    struct serv_params run_params;
    memset(&run_params, 0, sizeof(struct serv_params)); // per sicurezza azzero tutto
    int parse_status;
    if(argc == 3 && strncmp(argv[1], "-f", strlen(argv[1])) == 0) {
        // Se è stata passata l'opzione -f al server leggo il file di configurazione da essa
        parse_status = parse_config(&run_params, argv[2]);
    }
    else {
        // Altrimenti viene usato il file CONF_PATH_DFL
        parse_status = parse_config(&run_params, NULL);
    }

    int errcode = 0; // variabile usata per salvare errno e poi ritornare dal main con tale valore
    // errore di parsing: lo riporto su standard error ed esco con tale codice di errore
    if(parse_status == -1) {
        errcode = errno;
        fprintf(stderr,
                "[SERVER]: Fallito parsing del file di configurazione \"%s\": %s\n",
                (argv[1] == NULL ? CONF_PATH_DFL : argv[1]),
                strerror(errno));
        clean_server(&run_params, NULL);
        return errcode;
    }


    // inizializzo la struttura dati del server
    struct fs_ds_t *server_ds = NULL; // la struttura viene allocata in init_ds
    if(init_ds(&run_params, &server_ds) == -1) {
        errcode = errno;
        fprintf(stderr, "[SERVER]: Fallita inizializzazione strutture dati del server: %s\n", strerror(errno));
        clean_server(&run_params, server_ds);
        return errcode;
    }

    // Creo il socket sul quale il server ascolta connessioni
    int listen_connections;
    // se presente tolgo il socket prima di ricrearlo
    unlink(run_params.sock_path);
    if((listen_connections = sock_init(run_params.sock_path)) == -1) {
        errcode = errno; // salvo l'errore originato dalla funzione sock_init per ritornarlo
        fprintf(stderr, "[SERVER]: Impossibile creare socket \"%s\": %s\n", run_params.sock_path, strerror(errno));
        clean_server(&run_params, server_ds);
        return errcode;
    }

    // adesso disabilito tutti i segnali per il thread manager e per quelli da esso creati (workers)
    sigset_t mask_sigs;
    if(sigemptyset(&mask_sigs) == -1
        || sigaddset(&mask_sigs, SIGHUP) == -1
        || sigaddset(&mask_sigs, SIGINT) == -1
        || sigaddset(&mask_sigs, SIGQUIT) == -1) {
        errcode = errno;
        perror("[MANAGER THREAD]: Impossibile creare maschera segnali");
        // cleanup e poi esco
        clean_server(&run_params, server_ds);
        return errcode;
    }
    if((errcode = pthread_sigmask(SIG_BLOCK, &mask_sigs, NULL)) != 0) {
        errcode = errno;
        perror("[MANAGER THREAD]: Impossibile bloccare i segnali");
        // cleanup e poi esco
        clean_server(&run_params, server_ds);
        return errcode;
    }

    //-------------------------------------------------------------------------------
    // Con la creazione dei thread effettuata di seguito il processo diventa multithreaded

     // Creo il thread che gestisce il logging
    pthread_t logger_tid;
    // passo al thread la struttura dati condivisa server_ds
    if(pthread_create(&logger_tid, NULL, logging, server_ds) == -1) {
        errcode = errno;
        fprintf(stderr, "[SERVER]: Impossibile creare il thread di logging: %s\n", strerror(errno));
        clean_server(&run_params, server_ds);
        return errcode;
    }

    // Creo il thread che gestisce la terminazione
    pthread_t term_tid;
    // passo al thread la struttura dati condivisa server_ds
    if(pthread_create(&term_tid, NULL, term_thread, server_ds) == -1) {
        errcode = errno;
        if(put_logmsg(server_ds->log_thread_config, errno, "[SERVER]: Impossibile creare il thread di terminazione") == -1) {
            fprintf(stderr, "[SERVER]: Impossibile creare il thread di terminazione\n[SERVER]: Impossibile effettuare logging\n");
        }
        clean_server(&run_params, server_ds);
        return errcode;
    }

    // Creo il thread che gestisce il logging in modo asincrono (i thread inseriscono richieste di log)

    // Creo la thread pool
    pthread_t *workers = malloc(run_params.thread_pool * sizeof(pthread_t));
    if(!workers) {
        errcode = errno;
        if(put_logmsg(server_ds->log_thread_config, errno, "[SERVER]: Impossibile creare threadpool") == -1) {
            fprintf(stderr, "[SERVER]: Impossibile creare threadpool\n[SERVER]: Impossibile effettuare logging\n");
        }
        clean_server(&run_params, server_ds);
        return errcode;
    }
    long int i;
    for(i = 0; i < run_params.thread_pool; i++) {
        // Il puntatore alle strutture dati viene passato ad ogni worker thread come parametro
        if((errcode = pthread_create(&(workers[i]), NULL, work, server_ds)) != 0) {
            if(put_logmsg(server_ds->log_thread_config, errcode, "[SERVER]: Impossibile creare threadpool") == -1) {
                fprintf(stderr, "[SERVER]: Impossibile creare threadpool\n[SERVER]: Impossibile effettuare logging\n");
            }
            // Mando il segnale di terminazione manualmente al thread di terminazione
            // Quindi tale thread notificherà al manager la terminazione veloce
            // che procede come descritto nella relazione (con la differenza che i restanti worker
            // non sono mai creati
            pthread_kill(term_tid, SIGINT);
            clean_server(&run_params, server_ds);
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
    int max_fd_idx = listen_connections;
    if(server_ds->feedback[0] > max_fd_idx) {
        max_fd_idx = server_ds->feedback[0];
    }
    if(server_ds->termination[0] > max_fd_idx) {
        max_fd_idx = server_ds->termination[0];
    }

    // Esco da questo while soltanto se uno dei segnali di terminazione viene ricevuto e
    // gestito dal thread che si occupa della terminazione
    while(1) {
        // setto i descrittori da ascoltare con select
        fd_read_cpy = fd_read;
        // Il server seleziona i file descriptor pronti in lettura
        if(select(max_fd_idx + 1, &fd_read_cpy, NULL, NULL, NULL) == -1) {
            // Select interrota da un errore non gestito: logging + terminazione
            if(put_logmsg(server_ds->log_thread_config, errno, "[SERVER]: select() fallita") == -1) {
                fprintf(stderr, "[SERVER]: select() fallita\n[SERVER]: Impossibile effettuare logging\n");
            }
            // mando il segnale di terminazione manualmente al thread di terminazione
            pthread_kill(term_tid, SIGINT);
            // setto il descrittore della terminazione in modo da forzare l'esecuzione
            // della procedura di terminazione veloce
            FD_ZERO(&fd_read_cpy);
            FD_SET(server_ds->termination[0], &fd_read_cpy);
        }

        // max_fd_idx è aggiornato solo dopo il for, per cui devo salvare il nuovo valore in un temporaneo
        // che sarà modificato se si aggiungono nuovi client, cioè nuovi socket da ascoltare in lettura
        int new_maxfd = max_fd_idx;
        // Devo trovare i file descriptor pronti ed eseguire azioni di conseguenza
        for(fd = 0; fd < max_fd_idx + 1; fd++) {
            // Era pronto il descrittore di lettura dalla pipe di terminazione, per cui devo terminare il server
            if(fd == server_ds->termination[0] && FD_ISSET(fd, &fd_read_cpy)) {
                // Devo leggere dalla pipe per capire quale tipo di terminazione (veloce o lenta)
                int term;
                if(readn(server_ds->termination[0], &term, sizeof(term)) != sizeof(term)) {
                    perror("[MANAGER THREAD]: Impossibile leggere tipo di terminazione");
                    clean_server(&run_params, server_ds);
                    pthread_kill(pthread_self(), SIGKILL); // forzo la terminazione del server
                }
                if(term == SLOW_TERM) {
                    // in caso di terminazione lenta chiudo solo il socket su cui si accettano connessioni
                    if(close(listen_connections) == -1) {
                        perror("[MANAGER THREAD]: Impossibile chiudere il socket del server");
                    }
                    // devono essere comunque servite le richieste dei client connessi, quindi tolgo
                    // solo listen_connections da quelli ascoltati dalla select
                    FD_CLR(listen_connections, &fd_read);

                    LOCK_OR_KILL(server_ds, &(server_ds->mux_clients), server_ds);
                    // Se non vi è alcun worker connesso salta subito alla terminazione
                    if(server_ds->connected_clients == 0) {
                        UNLOCK_OR_KILL(server_ds, &(server_ds->mux_clients));
                        goto term;
                    }
                    UNLOCK_OR_KILL(server_ds, &(server_ds->mux_clients));
                    break; // altri socket pronti sono esaminati solo dopo aver aggiornato il set
                }
                else {
                    // In caso di terminazione veloce chiudo tutti i descrittori
                    // Viene effettuata di default anche se è stato letto un valore che non sia FAST_TERM dalla pipe
                    close_fd(&fd_read, max_fd_idx + 1);

                    LOCK_OR_KILL(server_ds, &(server_ds->mux_jobq), server_ds->job_queue);
                    // svuoto la coda di richieste (liberandole)
                    struct node_t *n = NULL;
                    while((n = pop(server_ds->job_queue)) != NULL) {
                        free(n->data);
                        free(n);
                    }
                    // Inserisco tante richieste di terminazione veloce quanti sono i worker
                    struct request_t *req = newrequest(FAST_TERM, 0, 0, 0);
                    for(size_t n = 0; n < run_params.thread_pool; n++) {
                        if(enqueue(server_ds->job_queue, req, sizeof(struct request_t), -1) == -1) {
                            // Se fallisce l'inserimento di una richiesta di terminazione devo
                            // forzare la terminazione del server, poichè rimarrebbero
                            // dei worker sospesi per un tempo indefinito
                            clean_server(&run_params, server_ds);
                            pthread_kill(pthread_self(), SIGKILL);
                        }
                    }
                    free(req);
                    // Quindi segnalo a tutti i thread worker che vi è una nuova richiesta
                    pthread_cond_broadcast(&(server_ds->new_job));
                    UNLOCK_OR_KILL(server_ds, &(server_ds->mux_jobq));
                    goto term; // salto ad attendere la terminazione
                }
            }
            // Se un client ha richiesto la connessione allora viene accettata e viene assegnato il socket
            if(fd == listen_connections && FD_ISSET(fd, &fd_read_cpy)) {
                int client_sock;
                if((client_sock = accept_connection(listen_connections)) == -1) {
                    // errore nella connessione al client, ma non fatale: il server va avanti normalmente
                    // tuttavia eseguo il logging dell'operazione
                    if(put_logmsg(server_ds->log_thread_config, errno, "[MANAGER THREAD]: Impossibile accettare la connessione di un client") == -1) {
                        fprintf(stderr, "[MANAGER THREAD]: Impossibile creare threadpool\n[SERVER]: Impossibile effettuare logging\n");
                    }
                    continue; // posso passare ad esaminare il prossimo socket pronto
                }
                // Se è stata accettata la connessione allora aggiungo il socket
                // assegnato al set di quelli ascoltati da select ed aggiungo il client
                // all'array di clients connessi
                if(add_connection(server_ds, client_sock) == -1) {
                    // impossibile aggiungere un client: chiudo il socket della connessione
                    close(client_sock);
                    // log dell'operazione fallita
                    if(put_logmsg(server_ds->log_thread_config, errno, "[SERVER]: Impossibile aggiungere la connessione allo stato del server") == -1) {
                        fprintf(stderr, "[SERVER]: Impossibile aggiungere la connessione allo stato del server\n[SERVER]: Impossibile effettuare logging\n");
                    }
                }
                else {
                    FD_SET(client_sock, &fd_read);
                    // Se necessario devo aggiornare il massimo indice dei socket ascoltati
                    if(new_maxfd < client_sock) {
                        new_maxfd = client_sock;
                    }
                }
            }
            // Se ho ricevuto feeback da un worker (operazione completata)
            else if(fd == server_ds->feedback[0] && FD_ISSET(fd, &fd_read_cpy)) {
                int sock = -1; // leggo il socket servito dal worker
                if(read(server_ds->feedback[0], (void*)&sock, sizeof(int)) == -1) {
                    if(put_logmsg(server_ds->log_thread_config, errno, "[SERVER]: Fallita lettura del feedback da un worker") == -1) {
                        fprintf(stderr, "[SERVER]: Fallita lettura del feedback da un worker\n[SERVER]: Impossibile effettuare logging\n");
                    }
                    continue; // passo al prossimo descrittore pronto
                }
                // Se il socket è un numero negativo diverso da -1 allora è un socket chiuso
                // Perciò non devo reimmetterlo nel set
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
                    LOCK_OR_KILL(server_ds, &(server_ds->mux_clients), server_ds);
                    if(sock < 0 && server_ds->slow_term == 1 && server_ds->connected_clients == 0) {
                        // In tal caso i worker sono terminati e posso saltare alla join
                        UNLOCK_OR_KILL(server_ds, &(server_ds->mux_clients));
                        goto term;
                    }
                    UNLOCK_OR_KILL(server_ds, &(server_ds->mux_clients));
                }
                break; // passo di nuovo alla select che comprenderà anche il socket reiserito
            }
            // Se ho ricevuto una richiesta da un client devo inserirla nella coda di richieste
            else if(FD_ISSET(fd, &fd_read_cpy)) {
                if(processRequest(server_ds, fd) == -1) {
                    // errore nel processing della richiesta
                    errcode = errno;
                    if(put_logmsg(server_ds->log_thread_config, errno, "[SERVER]: Fallito il processing di una richesta") == -1) {
                        fprintf(stderr, "[SERVER]: Fallito il processing di una richiesta\n[SERVER]: Impossibile effettuare logging\n");
                    }
                    // mando comunque una risposta al client, altrimenti si blocca
                    struct reply_t *reply = NULL;
                    if((reply = newreply(REPLY_NO, errno, 0, NULL)) == NULL) {
                        // errore allocazione risposta: il client presumibilmente va in stallo
                        // ma quantomeno è presente nel file di log la ragione
                        errcode = errno;
                        if(put_logmsg(server_ds->log_thread_config, errno, "[SERVER]: Fallita allocazione risposta") == -1) {
                            fprintf(stderr, "[SERVER]: Fallita allocazione risposta\n[SERVER]: Impossibile effettuare logging\n");
                        }
                    }
                    else if(writen(fd, reply, sizeof(*reply)) != sizeof(*reply)) {
                        // vale quanto commentato in precedenza
                        errcode = errno;
                        if(put_logmsg(server_ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta al client") == -1) {
                            fprintf(stderr, "[SERVER]: Fallito invio risposta al client\n[SERVER]: Impossibile effettuare logging\n");
                        }
                        free(reply);
                    }
                }
                // Altrimenti il processing della richiesta è andato a buon fine
                // Tolgo il fd da quelli ascoltati in attesa che qualche worker serva la richiesta
                // In questo modo faccio gestire la ricezione dei diversi tipi di richieste direttamente ai worker
                // altrimenti avrei dovuto aumentare la complessità di processRequest
                FD_CLR(fd, &fd_read);
            }
        }
        // Aggiorno il massimo fd da controllare nella select
        max_fd_idx = new_maxfd;
    }

// Terminazione del server: effettua il join dei thread worker
term:
    // stampo il tipo di terminazione
    printf("[SERVER] Terminazione %s\n", (server_ds->slow_term == 1 ? "lenta (SIGHUP)" : "veloce (SIGINT o SIGQUIT)"));

    // segnalo altri thread worker sospesi sulla coda di job perché se ho avuto terminazione lenta
    // e non vi è alcun client connesso allora essi rimarrebbero sospesi indefinitamente
    //(non controllerebbero mai la condizione di terminazione lenta)
    // segnalo cond variable senza mutex, perché non devono effettuare alcuna operazione sui dati
    pthread_cond_broadcast(&(server_ds->new_job));

    // Aspetto la terminazione dei worker thread (necessario per deallocare risorse)
    for(i = 0; i < run_params.thread_pool; i++) {
        if((errcode = pthread_join(workers[i], NULL)) != 0) {
            if(put_logmsg(server_ds->log_thread_config, errcode, "[SERVER]: Impossibile effettuare il join di un worker thread") == -1) {
                fprintf(stderr, "[SERVER]: Impossibile effettuare il join di un worker thread\n[SERVER]: Impossibile effettuare logging\n");
            }
            free(workers);
            stats(&run_params, server_ds);
            clean_server(&run_params, server_ds);
            return errcode;
        }
    }
    // libero memoria
    free(workers);

    // Effettuo il join del thread di terminazione (sarà già terminato in modo indipendente)
    if((errcode = pthread_join(term_tid, NULL)) != 0) {
        if(put_logmsg(server_ds->log_thread_config, errcode, "[SERVER]: Impossibile effettuare il join del thread di terminazione") == -1) {
            fprintf(stderr, "[SERVER]: Impossibile effettuare il join del thread di terminazione\n[SERVER]: Impossibile effettuare logging\n");
        }
        stats(&run_params, server_ds);
        clean_server(&run_params, server_ds);
        return errcode;
    }

    // Stampo su stdout le statistiche di uso del server
    stats(&run_params, server_ds);

    // Logging di alcuni parametri/valori rilevanti stampati anche su stdout
    char buf[BUF_BASESZ];
    memset(buf, 0, sizeof(char) * BUF_BASESZ);
    snprintf(buf,
            BUF_BASESZ,
            "[SERVER]: Max memoria utilizzata: %lu Mbytes (%lu bytes)",
            server_ds->max_used_mem/1048576, server_ds->max_used_mem);
    put_logmsg(server_ds->log_thread_config, 0, buf);
    snprintf(buf,
            BUF_BASESZ,
            "[SERVER]: Numero massimo di file: %lu",
            server_ds->max_nfiles);
    put_logmsg(server_ds->log_thread_config, 0, buf);
    snprintf(buf,
            BUF_BASESZ,
            "[SERVER]: Capacity misses: %lu",
            server_ds->cache_triggered);
    put_logmsg(server_ds->log_thread_config, 0, buf);
    snprintf(buf,
            BUF_BASESZ,
            "[SERVER]: Massimo numero di client connessi contemporaneamente: %lu",
            server_ds->max_connections);
    put_logmsg(server_ds->log_thread_config, 0, buf);


    // Inserisco il messaggio di log in coda per far terminare il thread di logging
    if(put_logmsg(server_ds->log_thread_config, -1, NULL) == -1) {
        fprintf(stderr, "[SERVER]: Fallito inserimento messaggio di terminazione logging\n");
        // posso anche terminare brutalmente, dato che è l'unica cosa rimasta in sospeso
        pthread_kill(pthread_self(), SIGKILL);
    }
    // Effettuo il join del thread per il logging (sarà già terminato in modo indipendente)
    if((errcode = pthread_join(logger_tid, NULL)) != 0) {
        fprintf(stderr, "[SERVER]: Impossibile effettuare il join del thread per il logging\n");
        stats(&run_params, server_ds);
        clean_server(&run_params, server_ds);
        return errcode;
    }

    // libero strutture dati del server
    clean_server(&run_params, server_ds);

    return 0;
}

void clean_server(struct serv_params *params, struct fs_ds_t *ds) {
    // rimuovo il socket dal filesystem
    if(params) {
        if(unlink(params->sock_path) == -1) {
            perror("Fallita rimozione socket");
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
    LOCK_OR_KILL(server_ds, &(server_ds->mux_jobq), server_ds->job_queue);
    if(enqueue(server_ds->job_queue, req, sizeof(struct request_t), client_sock) == -1) {
        return -1;
    }
    free(req);

    // Quindi segnalo ai thread worker che vi è una nuova richiesta
    pthread_cond_signal(&(server_ds->new_job));
    UNLOCK_OR_KILL(server_ds, &(server_ds->mux_jobq));

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
    // Ottengo il tempo corrente
    time_t now = time(NULL);
    // ottengo timestamp di avvio sotto forma di stringa
    char date_tm[27];
    memset(date_tm, 0, 27 * sizeof(char));
    ctime_r(&(ds->start_tm), date_tm);
    char *newln = strrchr(date_tm, '\n');
    *newln = '\0';
    printf("Server avviato %s: tempo di esecuzione %lds\n", date_tm, ((long int)now - (long int)ds->start_tm));
    printf("Numero di thread workers: %ld\n", params->thread_pool);
    printf("Socket del server: %s\n", params->sock_path);
    printf("File di log: %s\n", params->log_path);
    printf("Client connessi al momento della terminazione: %lu\n", ds->connected_clients);
    printf("Massimo numero di client connessi contemporaneamente: %lu\n", ds->max_connections);
    puts("======= MEMORIA =======");
    printf("Massima quantità di memoria %lu Mbyte (%lu byte)\n", ds->max_mem/1048576, ds->max_mem);
    printf("Massima quantità di memoria occupata : %lu Mbyte (%lu byte)\n", ds->max_used_mem/1048576, ds->max_used_mem);
    printf("Memoria in uso al momento della terminazione: %lu Mbyte (%lu byte)\n", ds->curr_mem/1048576, ds->curr_mem);
    printf("Chiamate algoritmo di rimpiazzamento FIFO: %lu\n", ds->cache_triggered);
    puts("======= FILES =======");
    printf("Massimo numero di file %lu\n", ds->max_files);
    printf("Massimo numero di file aperti durante l'esecuzione: %lu\n", ds->max_nfiles);
    printf("File presenti nel server al momento della terminazione: %lu\n", ds->curr_files);
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
