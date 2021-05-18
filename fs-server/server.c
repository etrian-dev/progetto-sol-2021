// header progetto
#include <utils.h>
#include <server-utils.h>
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

// Funzione main del server multithreaded: effettua il ruolo di manager thread
int main(int argc, char **argv) {
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
	printf("max memory size: %ld\n", run_params.max_memsz);
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
		if(log(logfile_fd, errno, "Impossibile inizializzare strutture dati server") == -1) {
			perror("Impossibile inizializzare strutture dati server");
		}
		return 1;
	}
	// assegno il descrittore del file di log alla struttura dati, per effettuare il logging
	// anche nei thread worker
	server_ds->log_fd = logfile_fd;


	// Creo il socket sul quale il server ascolta connessioni (e lo assegno)
	int listen_connections;
	if((listen_connections = sock_init(run_params.sock_path, strlen(run_params.sock_path))) == -1) {
		if(log(logfile_fd, errno, "Impossibile creare socket per ascolto connessioni") == -1) {
			perror("Impossibile creare socket per ascolto connessioni");
		}
		return 1;
	}

	//-------------------------------------------------------------------------------
	// Da qui in poi multithreaded

	// attributi per il thread di terminazione
	pthread_attr_t attrs;
	if(pthread_attr_init(&attrs) != 0 && pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED != 0)) {
		if(log(logfile_fd, errno, "Impossibile settare thread detached") == -1) {
			perror("Impossibile settare thread detached");
		}
		return 1;
	}
	// creo il thread che gestisce la terminazione
	struct term_params_t term_params;
	term_params.fast_term = 0;
	term_params.slow_term = 0;

	pthread_mutex_init(&(term_params.term_mux), NULL);
	if(pthread_create(&(term_params.term_tid), &attrs, term_thread, &term_params) == -1) {
		if(log(logfile_fd, errno, "Impossibile creare la thread di terminazione") == -1) {
				perror("Impossibile creare la thread di terminazione");
		}
		return 2;
	}

	// adesso disabilito tutti i segnali per il thread manager e per quelli da esso creati (worker)
	sigset_t mask_sigs;
	if(	sigemptyset(&mask_sigs) == -1
		|| sigaddset(&mask_sigs, SIGHUP) == -1
		|| sigaddset(&mask_sigs, SIGQUIT) == -1
		|| sigaddset(&mask_sigs, SIGINT) == -1) {
		if(log(logfile_fd, errno, "Impossibile creare maschera segnali") == -1) {
			perror("Impossibile creare maschera segnali");
		}
		return 1;
	}
	int ret;
	if((ret = pthread_sigmask(SIG_BLOCK, &mask_sigs, NULL)) != 0) {
		if(log(logfile_fd, ret, "Impossibile bloccare i segnali") == -1) {
			perror("Impossibile bloccare i segnali");
		}
		return 1;
	}

	// creo la thread pool
	pthread_t *workers = malloc(run_params.thread_pool * sizeof(pthread_t));
	if(!workers) {
		if(log(logfile_fd, errno, "Impossibile creare la threadpool") == -1) {
			perror("Impossibile creare thread pool");
		}
		return 1;
	}

	long int i;
	for(i = 0; i < run_params.thread_pool; i++) {
		// Il puntatore alle strutture dati viene passato ad ogni worker thread come parametro
		if(pthread_create(&workers[i], NULL, work, server_ds) != 0) {
			if(log(logfile_fd, errno, "Impossibile creare la threadpool") == -1) {
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
    int fast_term = 0;
    int slow_term = 0;
    while(!fast_term && !slow_term) {
        fd_read_cpy = fd_read;
        // Il server seleziona i file descriptor pronti
        if(select(max_fd_idx + 1, &fd_read_cpy, NULL, NULL, NULL) == -1) {
			if(errno == EINTR) {
				// era stata interrotta da un segnale gestito (terminazione)
				// per cui devo decidere se vanno chiuse le connessioni oppure no
				;
			}
			// un altro errore della select => provoca terminazione
			if(log(logfile_fd, errno, "Select fallita") == -1) {
				perror("Select fallita");
			}
			return 2;
        }

		// Devo trovare i file descriptor pronti
        for(fd = 0; fd < max_fd_idx; fd++) {
			// Se ci sono connessioni in attesa le accetta
            if(fd == listen_connections && FD_ISSET(fd, &fd_read_cpy)) {
				// La nuova connessione dovrà essere aggiunta alle strutture dati del server
                int client_sock;
                if((client_sock = accept_connection(listen_connections)) == -1) {
                    // errore nella connessione al client, ma non fatale: il server va avanti normalmente
                    // tuttavia eseguo il logging dell'operazione
                    if(log(logfile_fd, errno, "Impossibile accettare connessione") == -1) {
						perror("Impossibile accettare connessione");
					}
                    continue;
                }
                // Invece, se è stata accettata, allora si può aggiungere
                // il socket del client a quelli ascoltati da select
                FD_SET(client_sock, &fd_read);
                // Se necessario devo aggiornare il massimo indice dei socket
                if(max_fd_idx < client_sock) {
                    max_fd_idx = client_sock;
                }
            }
            // Se ho ricevuto feeback da un worker
            else if(fd == server_ds->feedback[0] && FD_ISSET(fd, &fd_read_cpy)) {
				// leggo il socket servito
				int sock = -1;
				read(server_ds->feedback[0], (void*)sock, sizeof(int)); // TODO: error checking
				// Rimetto il client tra quelli che il server ascolta (cioè in fd_read)
				FD_SET(sock, &fd_read);
                // Se necessario devo aggiornare il massimo indice dei socket
                if(max_fd_idx < sock) {
                    max_fd_idx = sock;
                }
            }
            // Se ho ricevuto una richiesta da un client devo inserirla nella coda di richieste
            else if(FD_ISSET(fd, &fd_read_cpy)) {
				int nbytes;
				char *req = malloc(BUF_BASESZ * sizeof(char)); // TODO: error checking
				size_t req_sz;
				size_t buf_offt = 0;
				while((nbytes = read(fd, req + buf_offt, BUF_BASESZ)) == BUF_BASESZ) {
					// Se ho letto esattamente quel numero di byte significa che la richiesta
					// non è ancora terminata, quindi rialloco il buffer di lettura
					if(rialloca_buffer(&req, req_sz + BUF_BASESZ) == -1) {
						// errore di (ri)allocazione
						// TODO: fatale o non fatale?
						if(log(logfile_fd, errno, "Errore di lettura della richiesta") == -1) {
							perror("Errore di lettura della richiesta");
						}
						return 2;
					}
					// aggiorno sia la size del buffer che l'offset in esso
					req_sz += BUF_BASESZ;
					buf_offt += BUF_BASESZ;
				}
				if(nbytes == -1) {
					if(log(logfile_fd, errno, "Errore di lettura della richiesta") == -1) {
						perror("Errore di lettura della richiesta");
					}
				}

				// Ora inserisco la richiesta nella coda (in ME)
			    if(pthread_mutex_lock(&(server_ds->mux_jobq)) == -1) {
					if(log(logfile_fd, errno, "Fallito lock coda di richieste") == -1) {
						perror("Fallito lock coda di richieste");
					}
				}
				if(enqueue(server_ds->job_queue, req, req_sz) == -1) {
					if(log(logfile_fd, errno, "Fallito inserimento nella coda di richieste") == -1) {
						perror("Fallito inserimento nella coda di richieste");
					}
				}
				if(pthread_mutex_unlock(&(server_ds->mux_jobq)) == -1) {
					if(log(logfile_fd, errno, "Fallito unlock coda di richieste") == -1) {
						perror("Fallito unlock coda di richieste");
					}
				}
				// Quindi segnalo ai thread worker che vi è una nuova richiesta
				pthread_cond_signal(&(server_ds->new_job));
			}
        }
	}

	for(i = 0; i < run_params.thread_pool; i++) {
		// Il puntatore alle strutture dati viene passato ad ogni worker thread come parametro
		if(pthread_join(workers[i], NULL) != 0) {
			if(log(logfile_fd, errno, "Impossibile joinare thread") == -1) {
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
