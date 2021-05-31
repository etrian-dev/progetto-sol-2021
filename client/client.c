// header client
#include <client.h>
// header API
#include <fs-api.h>
// header utilità
#include <utils.h>
// system call headers
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <dirent.h> // per visita directory
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stddef.h>

// macro usata per eseguire i comandi cmd solo se la flag è diversa da 0
#define PRINT(flag, cmd) if((flag)) {cmd;}

// Funzione main del processo client
int main(int argc, char **argv) {
    // processing degli argomenti riceuti da riga di comando
    // riempe i campi della struttura se ha successo
    struct client_opts *options = malloc(sizeof(struct client_opts));
    memset(options, 0, sizeof(struct client_opts)); // azzero per sicurezza

    errno = 0; // resetto errno per distinguere tra errori da syscall a errori nelle opzioni
    if(get_client_options(argc, argv, options) == -1) {
        // errore nel processare la lista di argomenti
        if(errno != 0) {
            fprintf(stderr, "Errore %d: %s\n", errno, strerror(errno));
        }
        // altrimenti è un errore specifico di sintassi delle opzioni, non di syscall o simili
        else {
            fprintf(stderr, "Errore di sintassi delle opzioni\n");
        }
        return 1;
    }

    // stampo le opzioni riconosciute
    PRINT(options->prints_on,
          printf("Help: %s\n", (options->help_on ? "ON" : "OFF"));
          printf("Stdout prints: %s\n", (options->prints_on ? "ON" : "OFF"));
          printf("Delay: %ldms\n", options->rdelay);
          printf("Socket: %s\n", options->fs_socket);
          printf("Directory swapout: %s\n", options->dir_swapout);
          printf("Directory with files to write: %s\n", options->dir_write);
          printf("Directory save reads: %s\n", options->dir_save_reads);
         )
    // Se è stato richiesto il messaggio di help stampa quello e poi esce
    if(options->help_on) {
        printf(HELP_MSG, argv[0]);
    }
    else {
        // Il client tenta di connettersi al server, tentando ogni rdelay millisecondi
        // Dopo 5 secondi di tentativi falliti esce con errore e termina il client
        struct timespec fivesec;
        clock_gettime(CLOCK_REALTIME, &fivesec);
        fivesec.tv_sec += 5;
        if(openConnection(options->fs_socket,options->rdelay, fivesec) == -1) {
            perror("Impossible connettersi al server");
            free_client_opt(options);
            return 1;
        }
        PRINT(options->prints_on, printf("[%d] Connesso al server\n", getpid());)

        // Invio le richieste nell'ordine in cui sono state date al client
        struct node_t *req = NULL;
        struct node_t *elem = NULL;
        char *path = NULL;
        char op_type;

        while((req = pop(options->oplist)) != NULL) {
            op_type = ((struct operation *)req->data)->type;
            while((elem = pop(((struct operation *)req->data)->flist)) != NULL) {
                path = (char*)elem->data;
                switch(op_type) {
                case READ_FILE: {
                    if(openFile(path, 0) == -1) {
                        // errore di apertura: log su stderr
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile aprire il file \"%s\"\n", getpid(), path);
                              perror("openFile");
                             );
                        break;
                    }
                    // file aperto, ora lo leggo
                    void *buf = NULL;
                    size_t file_sz = 0;
                    if(readFile(path, &buf, &file_sz) == -1) {
                        // errore di lettura
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile leggere il file \"%s\"\n", getpid(), path);
                              perror("readFile");
                             );
                        break;
                    }

                    // Mostro i dati letti a video
                    write(1, buf, file_sz);

                    if(closeFile(path) == -1) {
                        // errore di chiusura: log su stderr
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile chiudere il file \"%s\"\n", getpid(), path);
                              perror("closeFile");
                             );
                    }
                    break;
                }
                case WRITE_FILE: {
                    // apro il file con la flag per crearlo
                    // TODO: OR con O_LOCK?
                    if(openFile(path, O_CREATE) == -1) {
                        // errore di apertura: log su stderr
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile aprire il file \"%s\"\n", getpid(), path);
                              perror("openFile");
                             );
                        break;
                    }
                    // file aperto: scrivo il file e fornisco la cartella dove salvare eventuali file espulsi
                    if(writeFile(path, options->dir_swapout) == -1) {
                        // errore di lettura
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile scrivere il file \"%s\"\n", getpid(), path);
                              perror("writeFile");
                             );
                        break;
                    }
                    if(closeFile(path) == -1) {
                        // errore di chiusura: log su stderr
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile chiudere il file \"%s\"\n", getpid(), path);
                              perror("closeFile");
                             );
                    }
                    break;
                }
                case APPEND_FILE: {
                    // filename è una stringa con formato dest:src
                    // devo determinare se src è il path di un file oppure una stringa
                    size_t file_sz = 0;
                    void *buf = NULL;
                    // tokenizzo filename nei due campi
                    char *status = NULL;
                    char *dest = strtok_r(path, ":", &status);
                    char *src = strtok_r(NULL, "\n", &status);

                    // cerco di aprire il path src: se non è possibile allora lo interpreto come stringa
                    int file_fd;
                    if((file_fd = open(src, O_RDONLY)) == -1) {
                        // devo duplicare src in buf
                        file_sz = strlen(src) + 1;
                        buf = (void*)strndup(src, file_sz);
                    }
                    else {
                        // Era il path di un file, quindi con fstat ottengo informazioni sul file
                        // (la dimensione ed il tipo di file sono le uniche rilevanti in questo contesto)
                        struct stat info;
                        memset(&info, 0, sizeof(info));
                        if(fstat(file_fd, &info) == -1) {
                            // impossibile ottenere info
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[CLIENT %d]: Impossibile ottenere info sul file \"%s\"\n", getpid(), src);
                                  perror("fstat");
                                 );
                            break;
                        }
                        // Controllo che sia un file regolare, ne prendo la dimensione e lo leggo in un buffer
                        if(S_ISREG(info.st_mode)) {
                            file_sz = info.st_size;
                            if((buf = malloc(file_sz)) == NULL) {
                                // impossibile allocare il buffer per il file da concatenare
                                PRINT(options->prints_on,
                                      fprintf(stderr, "[CLIENT %d]: Allocazione buffer fallita\n", getpid());
                                      perror("malloc");
                                     );
                                break;
                            }
                            // leggo il file nel buffer
                            size_t tot = file_sz;
                            int written = 0;
                            while(written != -1 && tot > 0) {
                                written = readn(file_fd, buf + file_sz - tot, tot);
                                tot -= written;
                            }
                            if(written == -1) {
                                // impossibile leggere il file
                                PRINT(options->prints_on,
                                      fprintf(stderr, "[CLIENT %d]: Lettura file fallita\n", getpid());
                                      perror("read");
                                     );
                                free(buf);
                                break;
                            }
                        }
                        else {
                            // non è un file regolare
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[CLIENT %d]: \"%s\" non è un file regolare\n", getpid(), src);
                                  perror("fstat");
                                 );
                            free(buf);
                            break;
                        }
                    }
                    // A questo punto src contiene il path (o stringa) e
                    // buf contiene i dati e file_sz la size di buf

                    // apro il file nel server
                    // TODO: flag O_LOCK?
                    if(openFile(dest, 0) == -1) {
                        // errore di apertura: log su stderr
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile aprire il file \"%s\"\n", getpid(), dest);
                              perror("openFile");
                             );
                        break;
                    }

                    // file aperto: scrivo in append il file e fornisco la cartella dove salvare eventuali file espulsi
                    if(appendToFile(dest, buf, file_sz, options->dir_swapout) == -1) {
                        // errore di append
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile concatenare il file \"%s\"\n", getpid(), dest);
                              perror("appendToFile");
                             );
                        break;
                    }
                    if(closeFile(dest) == -1) {
                        // errore di chiusura: log su stderr
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile chiudere il file \"%s\"\n", getpid(), dest);
                              perror("closeFile");
                             );
                    }
                    break;
                }
                default:
                    fprintf(stderr, "Operazione non implementata");
                }

                // libero memoria allocata per il file
                free(elem->data);
                free(elem);
            }
            // libero la memoria allocata per la richiesta
            //free(req->data);
            free(req);
        }
        // libero la lista di operazioni
        free(options->oplist);

        // Adesso viene eseguita la (eventuale) scrittura di file da una directory
        // il cui path è in options->dir_write
        if(options->dir_write) {
            // visita ricorsivamente a partire da dir_write e restituisce una lista di file
            struct Queue *lfiles = queue_init();
            long int nfiles = visit_dir(lfiles, options->dir_write, options->nwrite);
            printf("Letti %ld files dalla directory\n", nfiles);

            // scrivo i file nella coda nel fileserver
            while((elem = pop(lfiles)) != NULL) {
                path = (char*)elem->data;
                // apro il file con la flag per crearlo
                // TODO: OR con O_LOCK?
                if(openFile(path, O_CREATE) == -1) {
                    // errore di apertura: log su stderr
                    PRINT(options->prints_on,
                          fprintf(stderr, "[CLIENT %d]: Impossibile aprire il file \"%s\"\n", getpid(), path);
                          perror("openFile");
                         );
                    break;
                }
                // file aperto: scrivo il file e fornisco la cartella dove salvare eventuali file espulsi
                if(writeFile(path, options->dir_swapout) == -1) {
                    // errore di lettura
                    PRINT(options->prints_on,
                          fprintf(stderr, "[CLIENT %d]: Impossibile scrivere il file \"%s\"\n", getpid(), path);
                          perror("writeFile");
                         );
                    break;
                }
                if(closeFile(path) == -1) {
                    // errore di chiusura: log su stderr
                    PRINT(options->prints_on,
                        fprintf(stderr, "[CLIENT %d]: Impossibile chiudere il file \"%s\"\n", getpid(), path);
                        perror("closeFile");
                    );
                }

                // libero la memoria dell'elemento della coda
                free(elem->data);
                free(elem);
            }
        }

        // Adesso viene eseguita la (eventuale) lettura di n file qualsiasi (dal punto di vista del client)
        /// Se non è -1 allora significa che l'opzione è stata riconosciuta correttamente
        // Altrimenti o non è stata data questa opzione, oppure aveva un argomento non valido
        if(options->nread != -1) {
            // visita ricorsivamente a partire da dir_write e restituisce una lista di file
            struct Queue *lfiles = queue_init();
            long int nfiles = visit_dir(lfiles, options->dir_save_reads, options->nread);
            printf("Da leggere %ld files\n", nfiles);

            // invio nreads richieste di lettura: se è stata settata anche la directory per salvare
            // i file letti allora vengono creati lì con il loro pathname
            if(readNFiles(options->nread, options->dir_save_reads) == -1) {
                // errore di lettura
                PRINT(options->prints_on,
                    if(options->nread == 0) {
                        fprintf(stderr, "[CLIENT %d]: Impossibile leggere files\n", getpid());
                    }
                    else {
                        fprintf(stderr, "[CLIENT %d]: Impossibile leggere %ld files\n", getpid(), options->nread);
                    }
                    perror("readNFiles");
                );
            }
        }

        // La connessione con il server viene chiusa
        if(closeConnection(options->fs_socket) == -1) {
            perror("Impossible disconnettersi dal server");
            return 1;
        }
        PRINT(options->prints_on, printf("[%d] Disconnesso dal server\n", getpid()));
    }
    // libera la struttra dati contenente le opzioni del client
    free_client_opt(options);

    return 0;
}


