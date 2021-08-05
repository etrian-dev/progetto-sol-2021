// header client
#include <client.h>
// header API
#include <fs-api.h>
// header utilità
#include <utils.h>
// system call headers
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// macro usata per eseguire i comandi cmd solo se la flag è diversa da 0
#define PRINT(flag, cmd) if((flag)) {cmd;fflush(stdout);}

// Funzione main del processo client
int main(int argc, char **argv) {
    // Ottengo il PID del processo client una volta per usarlo poi in tutte le stampe
    int PID = getpid();

    // processing degli argomenti riceuti da riga di comando
    // assegna dei valori ai campi della struttura se ha successo
    struct client_opts *options = init_params();
    if(!options) {
        fprintf(stderr, "[%d]: Fallita allocazione opzioni\n", PID);
        return 1;
    }

    errno = 0; // resetto errno per distinguere tra errori da syscall a errori nelle opzioni
    if(get_client_options(argc, argv, options) == -1) {
        // errore nel processare la lista di argomenti
        if(errno != 0) {
            fprintf(stderr, "[%d]: Errore %d: %s\n", PID, errno, strerror(errno));
        }
        // altrimenti è un errore specifico di sintassi delle opzioni, non di syscall o simili
        else {
            fprintf(stderr, "[%d]: Errore di sintassi delle opzioni\n", PID);
        }
        return 1;
    }

    // Se è stato richiesto il messaggio di help stampa quello e poi esce
    // Vi sono altre condizioni  di seguito
    if(options->help_on || argc == 1 || options->fs_socket == NULL) {
        printf(HELP_MSG, argv[0]);
    } else {
        // Il client tenta di connettersi al server, tentando ogni rdelay millisecondi
        // Dopo 5 secondi di tentativi falliti esce con errore e termina il client
        struct timespec fivesec;
        clock_gettime(CLOCK_REALTIME, &fivesec);
        fivesec.tv_sec += 5;
        if(openConnection(options->fs_socket, options->rdelay, fivesec) == -1) {
            fprintf(stderr, "[%d]: Impossible connettersi al server sul socket %s\n", PID, options->fs_socket);
            free_client_opt(options);
            return 1;
        }
        PRINT(options->prints_on, printf("[%d]: Connesso al server sul socket %s\n", PID, options->fs_socket);)

        struct timespec waitfor; // struttura per delay
        if(options->rdelay > 0) {
            get_delay(options->rdelay, &waitfor);
        }

        // Invio le richieste nell'ordine in cui sono state date al client
        struct node_t *req = NULL;
        struct node_t *elem = NULL;
        char *path = NULL;

        // Scorre la lista di operazioni richieste
        while((req = pop(options->oplist)) != NULL) {
            // salvo l'operazione in una struttura per evitare di fare tanti cast
            struct operation *op = (struct operation *)req->data;
            switch(op->type) {
            case 'w': { // l'opzione -w viene trattata in modo speciale
                // Visita ricorsivamente a partire da op->dir_op e restituisce una lista di path
                // il cui numero è <= op->max_n (o non limitato superiormente se tale campo è < 0)
                struct Queue *lfiles = queue_init(); // inizializzo una coda vuota che conterrà i path dei file da scrivere
                struct Queue *ldata = queue_init(); // inizializzo una coda vuota che conterrà i dati dei file da scrivere
                long int nfiles = visit_dir(lfiles, ldata, op->dir_op, op->max_n);
                if(nfiles == -1) {
                    // errore nel leggere file da dir_op
                    PRINT(options->prints_on, fprintf(stderr, "[%d]: Impossibile leggere files dalla directory \"%s\": %s\n", PID, op->dir_op, strerror(errno));)
                } else {
                    PRINT(options->prints_on, fprintf(stdout, "[%d]: Letti %ld files dalla directory \"%s\"\n", PID, nfiles, op->dir_op);)
                    struct node_t *file_data = NULL;
                    // scrivo i file presenti nella coda all'interno del fileserver
                    while((elem = pop(lfiles)) != NULL && (file_data = pop(ldata)) != NULL) {
                        path = (char*)elem->data;
                        int success = 0;
                        // apro il file con la flag per crearlo
                        if((success = openFile(path, O_CREATEFILE|O_LOCKFILE)) == -1) {
                            // errore di apertura: log su stderr
                            fprintf(stderr, "[%d]: Impossibile creare in modo locked il file \"%s\"\n", PID, path);
                        }
                        // file aperto: lo scrivo e fornisco la cartella dove salvare eventuali espulsioni
                        if(success == 0) {
                            if(writeFile(path, op->dir_swp) == -1) {
                                // errore di lettura
                                PRINT(options->prints_on,
                                      fprintf(stderr, "[%d]: Impossibile scrivere il file \"%s\"\n", PID, path);
                                     )
                            }
                        }
                        // Chiudo il file (anche se la scrittura fosse fallita)
                        if((success = closeFile(path)) == -1) {
                            // errore di chiusura: log su stderr
                            PRINT(options->prints_on, fprintf(stderr, "[%d]: Impossibile chiudere il file \"%s\"\n", PID, path);)
                        }
                        // libero la memoria occupata
                        free(path);
                        free(elem);
                        free(file_data->data);
                        free(file_data);
                    }
                }
                free_Queue(lfiles, free);
                free_Queue(ldata, free);
                break;
            }
            case READ_N_FILES: { // l'opzione -R viene trattata in modo speciale
                // invio nreads richieste di lettura: se è stata settata anche la directory per salvare
                // i file letti allora vengono creati lì con il loro nome file (no path)
                int n;
                if((n = readNFiles(op->max_n, op->dir_swp)) == -1) {
                    // errore di lettura
                    PRINT(options->prints_on,
                    if(op->max_n <= 0) {
                    fprintf(stderr, "[%d]: Impossibile leggere alcun file da \"%s\"\n", PID, op->dir_swp);
                    } else {
                        fprintf(stderr, "[%d]: Impossibile leggere %ld files da \"%s\"\n", PID, op->max_n, op->dir_swp);
                    }
                         )
                } else {
                    PRINT(options->prints_on,
                          printf("[%d]: Ricevuti %d files dal server\n", PID, n);
                    if(op->dir_swp) {
                    printf("[%d]: Salvati nella directory \"%s\"\n", PID, op->dir_swp);
                    }
                         )
                }
                break;
            }
            // tutti trattati fondamentalmente allo stesso modo
            case READ_FILE:
            case APPEND_FILE:
            case WRITE_FILE:
            case LOCK_FILE:
            case UNLOCK_FILE:
            case REMOVE_FILE: {
                // Per ogni richiesta vi è una lista di file sui quali operare
                while((elem = pop(op->flist)) != NULL) {
                    // Il path del file è il campo dati (casting necessario perché li memorizzo come void*)
                    path = (char*)elem->data;
                    // A seconda del tipo di richiesta chiamo le operazioni appropriate
                    switch(op->type) {
                    case READ_FILE: { // Operazione di lettura: apro il file, lo leggo e poi lo chiudo
                        if(openFile(path, 0) == -1) { // apro senza alcuna flag
                            // errore di apertura: log su stderr
                            PRINT(options->prints_on,fprintf(stderr, "[%d]: Impossibile aprire il file \"%s\"\n", PID, path);)
                            break;
                        }
                        // file aperto: ora lo leggo
                        void *buf = NULL;
                        size_t file_sz = 0;
                        if(readFile(path, &buf, &file_sz) == -1) {
                            // errore di lettura
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile leggere il file \"%s\"\n", PID, path);
                                 )
                        }
                        // Mostro l'avvenuta lettura
                        PRINT(options->prints_on, fprintf(stdout, "[%d]: Letto il file \"%s\" (%lu bytes)\n", PID, path, file_sz);)
                        // Se era stata settata la directory per salvarlo lo salvo, altrimenti viene liberata la memoria
                        if(op->dir_swp) {
                            int success = 0;
                            int ffd = -1;
                            char *only_fname = strrchr(path, '/');
                            only_fname += 1;
                            char *saved_fpath = get_fullpath(op->dir_swp, only_fname);
                            if(saved_fpath) {
                                if((ffd = open(saved_fpath, O_WRONLY|O_CREAT|O_TRUNC, PERMS_ALL_READ)) == -1) {
                                    success = -1;
                                }
                                if(success == 0 && writen(ffd, buf, file_sz) != file_sz) {
                                    success = -1;
                                }
                            }
                            else {
                                success = -1;
                            }
                            if(success == -1) {
                                PRINT(options->prints_on,
                                    fprintf(stderr, "[%d]: Impossibile salvare il file \"%s\" nella directory \"%s\": %s\n",
                                        getpid(),
                                        path,
                                        op->dir_swp,
                                        strerror(errno)
                                        );
                                )
                            }
                            if(saved_fpath) free(saved_fpath);
                            if(buf) free(buf);
                            if(ffd != -1) close(ffd);
                        }
                        // Chiudo il file
                        if(closeFile(path) == -1) {
                            // errore di chiusura: log su stderr
                            PRINT(options->prints_on,
                                fprintf(stderr, "[%d]: Impossibile chiudere il file \"%s\"\n", PID, path);
                            )
                        }
                        break;
                    }
                    case WRITE_FILE: {
                        // apro il file con la flag per crearlo in modo locked
                        if(openFile(path, O_CREATEFILE|O_LOCKFILE) == -1) {
                            // errore di apertura: log su stderr
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile creare in modo locked il file \"%s\"\n", PID, path);
                                 )
                            break;
                        }
                        // File aperto: scrivo il file e fornisco la cartella dove salvare eventuali file espulsi
                        // Se dir_swp fosse NULL i file restituiti vengono liberati internamente dalla API
                        if(writeFile(path, op->dir_swp) == -1) {
                            // errore di lettura
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile scrivere il file \"%s\"\n", PID, path);
                                 )
                        }
                        // Mostro l'avvenuta scrittura
                        PRINT(options->prints_on, printf("[%d]: Scritto il file \"%s\"\n", PID, path);)
                        // Chiudo il file
                        if(closeFile(path) == -1) {
                            // errore di chiusura: log su stderr
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile chiudere il file \"%s\"\n", PID, path);
                                 )
                        }
                        break;
                    }
                    case APPEND_FILE: {
                        // path è una stringa con formato dest:src
                        // devo determinare se src è il path di un file oppure una stringa
                        // tokenizzo filename nei due campi
                        char *status = NULL;
                        char *dest = strtok_r(path, ":", &status);
                        char *src = strtok_r(NULL, "\n", &status);
                        void *buf = NULL;
                        size_t file_sz = 0;

                        // cerco di aprire il path src: se non è possibile allora lo interpreto come stringa
                        int file_fd;
                        if((file_fd = open(src, O_RDONLY)) == -1) {
                            // devo duplicare src in buf
                            file_sz = strlen(src) + 1;
                            buf = (void*)strndup(src, file_sz);
                            if(!buf) {
                                // errore nella duplicazione della stringa
                                PRINT(options->prints_on,
                                      fprintf(stderr, "[%d]: Impossibile duplicare la stringa \"%s\"\n", PID, src);
                                      perror("strndup");
                                     )
                                break;
                            }
                        } else {
                            // Era il path di un file, quindi con fstat ottengo informazioni sul file
                            // (la dimensione ed il tipo di file sono le uniche rilevanti in questo contesto)
                            struct stat info;
                            memset(&info, 0, sizeof(info));
                            if(fstat(file_fd, &info) == -1) {
                                // impossibile ottenere informazioni
                                PRINT(options->prints_on,
                                      fprintf(stderr, "[%d]: Impossibile ottenere info sul file \"%s\"\n", PID, src);
                                      perror("fstat");
                                     )
                                break;
                            }
                            // Controllo che sia un file regolare: se lo è ne prendo la dimensione e lo leggo in un buffer
                            if(S_ISREG(info.st_mode)) {
                                file_sz = info.st_size;
                                if((buf = malloc(file_sz)) == NULL) {
                                    // impossibile allocare il buffer per il file da concatenare
                                    PRINT(options->prints_on,
                                          fprintf(stderr, "[%d]: Allocazione buffer fallita\n", PID);
                                          perror("malloc");
                                         )
                                    break;
                                }
                                // leggo il file nel buffer
                                if(readn(file_fd, buf, file_sz) != file_sz) {
                                    // impossibile leggere il file
                                    PRINT(options->prints_on,
                                          fprintf(stderr, "[%d]: Lettura file  \"%s\" da concatenare fallita\n", PID, src);
                                          perror("read");
                                         )
                                    free(buf);
                                    break;
                                }
                            } else {
                                // non è un file regolare
                                PRINT(options->prints_on, fprintf(stderr, "[%d]: \"%s\" non è un file regolare\n", PID, src);)
                                free(buf);
                                break;
                            }
                        }
                        // A questo punto dest contiene il path del file nel server
                        // e buf contiene i dati da scrivere e file_sz la dimensione di buf

                        // apro il file nel server
                        if(openFile(dest, 0) == -1) {
                            // errore di apertura: log su stderr
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile aprire il file \"%s\"\n", PID, dest);
                                 )
                            break;
                        }
                        // file aperto: scrivo in append il file e fornisco la cartella dove salvare eventuali file espulsi
                        if(appendToFile(dest, buf, file_sz, op->dir_swp) == -1) {
                            // errore di append
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile concatenare il file \"%s\"\n", PID, dest);
                                 )
                        }

                        PRINT(options->prints_on,
                              fprintf(stdout, "[%d]: Concatenato \"%s\" (%lu bytes) al file \"%s\"\n", PID, src, file_sz, path);
                             )

                        if(closeFile(dest) == -1) {
                            // errore di chiusura: log su stderr
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile chiudere il file \"%s\"\n", PID, dest);
                                 )
                        }
                        break;
                    }
                    case LOCK_FILE: {
                        // Tenta di aprire il file con flag O_LOCKFILE
                        if(openFile(path, O_LOCKFILE) == -1) {
                            // errore di apertura: log su stderr
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile aprire il file in modo locked \"%s\"\n", PID, path);
                                 )
                        }
                        // Tenta di acquisire la mutua esclusione sul file (indipendentemente dall'esito della openFile)
                        // La API prova ripetutamente a farlo ogni 100 millisecondi
                        if(lockFile(path) == -1) {
                            // Errore nel settare mutua esclusione
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile settare mutua esclusione sul file \"%s\"\n", PID, path);
                                 )
                        } else {
                            PRINT(options->prints_on,
                                  fprintf(stdout, "[%d]: Mutua esclusione sul file \"%s\" acquisita con successo\n", PID, path);
                                 )
                        }
                        break;
                    }
                    case UNLOCK_FILE: {
                        // Tenta di aprire il file con flag O_LOCKFILE
                        if(openFile(path, O_LOCKFILE) == -1) {
                            // errore di apertura: log su stderr
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile aprire il file in modo locked \"%s\"\n", PID, path);
                                 )
                        }
                        // Rilascia la mutua esclusione sul file (indipendentemente dall'esito della openFile)
                        if(unlockFile(path) == -1) {
                            // Errore nel rimuovere mutua esclusione (o il client non la aveva settata o altro)
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile rimuovere mutua esclusione dal file \"%s\"\n", PID, path);
                                 )
                        } else {
                            PRINT(options->prints_on,
                                  fprintf(stdout, "[%d]: Mutua esclusione tolta sul file \"%s\"\n", PID, path);
                                 )
                        }
                        break;
                    }
                    case REMOVE_FILE: {
                        // Tenta di aprire il file con flag O_LOCKFILE
                        if(openFile(path, O_LOCKFILE) == -1) {
                            // errore di apertura: log su stderr
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile aprire il file in modo locked \"%s\"\n", PID, path);
                                 )
                        }
                        // Tenta di rimuovere il file dal server indipendentemente dall'esito dell'operazione precedente
                        if(removeFile(path) == -1) {
                            // Errore nel rimuovere il file dal server
                            // può essere causato dal non aver aperto il file o non essere in possesso
                            // della mutua esclusione su di esso
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[%d]: Impossibile rimuovere il file \"%s\" dal filserver\n", PID, path);
                                 )
                        } else {
                            PRINT(options->prints_on,
                                  fprintf(stdout, "[%d]: Rimozione del file \"%s\" completata con successo\n", PID, path);
                                 )
                        }
                        break;
                    }
                    }
                }
                break;

            }
            break;
            default:
                fprintf(stderr, "\'%c\': Operazione non implementata\n", op->type);
            }
            // libero la richiesta
            free_op(&op);
            free(req);
            req = NULL;

            // Aspetto un tempo dato dal delay settato tramite la riga di comando
            if(options->rdelay > 0) {
                nanosleep(&waitfor, NULL);
            }
        }
        free(options->oplist);
        options->oplist = NULL;

        // La connessione con il server viene chiusa
        if(closeConnection(options->fs_socket) == -1) {
            PRINT(options->prints_on, fprintf(stderr, "[%d]: Impossible disconnettersi dal server\n", PID);)
            return 1;
        }
        PRINT(options->prints_on, printf("[%d]: Disconnesso dal server\n", PID));
    }
    // libera la struttra dati contenente le opzioni del client
    free_client_opt(options);

    return 0;
}


