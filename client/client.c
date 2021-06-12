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
#define PRINT(flag, cmd) if((flag)) {cmd;}

// Funzione per salvare i file letti nella directory dir; Se ha successo ritorna 0, -1 altrimenti
int save_reads(const char *dir, const int nbufs, const void **bufs, const size_t *sizes);

// Funzione main del processo client
int main(int argc, char **argv) {
    // processing degli argomenti riceuti da riga di comando
    // assegna dei valori ai campi della struttura se ha successo
    struct client_opts *options = init_params();
    if(!options) {
        fprintf(stderr, "Allocazione parametri client fallita. Terminazione...");
        return -1;
    }

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
        free_client_opt(options);
        return 1;
    }

    // Se è stato richiesto il messaggio di help stampa quello e poi esce
    // Vi sono altre condizioni  di seguito
    if(options->help_on || argc == 1 || options->fs_socket == NULL) {
        printf(HELP_MSG, argv[0]);
    }
    else {
        // Il client tenta di connettersi al server, tentando ogni rdelay millisecondi
        // Dopo 5 secondi di tentativi falliti esce con errore e termina il client
        struct timespec fivesec;
        clock_gettime(CLOCK_REALTIME, &fivesec);
        fivesec.tv_sec += 5;
        if(openConnection(options->fs_socket, options->rdelay, fivesec) == -1) {
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

        // Scorre la lista di operazioni richieste
        while((req = pop(options->oplist)) != NULL) {
            struct operation *op = req->data;
            op_type = op->type;
            // Per ogni richiesta vi è una lista di file sui quali operare
            while((elem = pop(op->flist)) != NULL) {
                // Il path del file è il campo dati (casting necessario perché li memorizzo come void*)
                path = (char*)elem->data;

                // A seconda del tipo di richiesta chiamo le operazioni appropriate (read, write o append)
                switch(op_type) {
                case 'r': {
                    // Operazione di lettura: apro il file, lo leggo e poi lo chiudo
                    if(openFile(path, 0) == -1) {
                        // errore di apertura: log su stderr
                        PRINT(options->prints_on,
                            fprintf(stderr, "[CLIENT %d]: Impossibile aprire il file \"%s\"\n", getpid(), path);
                            perror("openFile");
                        )
                        break;
                    }
                    // file aperto: ora lo leggo
                    void *buf = NULL;
                    size_t file_sz = 0;
                    if(readFile(path, &buf, &file_sz) == -1) {
                        // errore di lettura
                        PRINT(options->prints_on,
                            fprintf(stderr, "[CLIENT %d]: Impossibile leggere il file \"%s\"\n", getpid(), path);
                            perror("readFile");
                        )
                    }
                    else {
                        // Mostro l'avvenuta lettura se è stata passata -p
                        PRINT(options->prints_on, printf("Letto il file %s: %lu bytes\n", path, file_sz))
                        // Se è stata settata la directory per salavare i file lo faccio
                        if(op->dirname) {
                            if(save_reads(op->dirname, 1, &buf, &file_sz) == -1) {
                                fprintf(stderr, "Fallito salvataggio del file \"%s\"\n", path);
                            }
                        }
                        // In ogni caso devo liberare la memoria occupata dal file letto
                        free(buf);
                    }
                    if(closeFile(path) == -1) {
                        // errore di chiusura: log su stderr
                        PRINT(options->prints_on,
                            fprintf(stderr, "[CLIENT %d]: Impossibile chiudere il file \"%s\"\n", getpid(), path);
                            perror("closeFile");
                        )
                    }
                    break;
                }
                case 'R': {
                    // Invio al server la richiesta di leggere al più max_read files ed opzionalmente
                    // di salvarli in dirname
                    int n;
                    if((n = readNFiles(op->max_read, op->dirname)) == -1) {
                        // errore di lettura
                        PRINT(options->prints_on,
                            fprintf(stderr, "[CLIENT %d]: Impossibile leggere files\n", getpid());
                            perror("readNFiles");
                        )
                    }
                    else {
                        PRINT(options->prints_on,
                            printf("Ricevuti %d files dal server\n", n);
                        )
                    }
                    break;
                }
                case 'a': {
                    // path è una stringa con formato dest:src
                    // devo determinare se src è il path di un file oppure una stringa
                    size_t file_sz = 0;
                    void *buf = NULL;
                    // tokenizzo filename nei due campi
                    char *status = NULL;
                    char *dest = strtok_r(path, ":", &status);
                    char *src = strtok_r(NULL, "\n", &status);

                    // cerco di aprire il file src: se non è possibile allora lo interpreto come stringa
                    int file_fd;
                    if((file_fd = open(src, O_RDONLY)) == -1) {
                        // devo duplicare src in buf
                        file_sz = strlen(src) + 1;
                        buf = (void*)strndup(src, file_sz);
                        if(!buf) {
                            // errore nella duplicazione della stringa
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[CLIENT %d]: Impossibile duplicare \"%s\"\n", getpid(), src);
                                  perror("strndup");
                            )
                            break;
                        }
                    }
                    else {
                        // Era il path di un file, quindi con fstat ottengo informazioni sul file
                        // (la dimensione ed il tipo di file sono le uniche rilevanti in questo contesto)
                        struct stat info;
                        memset(&info, 0, sizeof(info));
                        if(fstat(file_fd, &info) == -1) {
                            // impossibile ottenere informazioni
                            PRINT(options->prints_on,
                                  fprintf(stderr, "[CLIENT %d]: Impossibile ottenere info sul file \"%s\"\n", getpid(), src);
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
                                      fprintf(stderr, "[CLIENT %d]: Allocazione buffer fallita\n", getpid());
                                      perror("malloc");
                                     )
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
                                     )
                                free(buf);
                                break;
                            }
                        }
                        else {
                            // non è un file regolare
                            PRINT(options->prints_on, fprintf(stderr, "[CLIENT %d]: \"%s\" non è un file regolare\n", getpid(), src);)
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
                              fprintf(stderr, "[CLIENT %d]: Impossibile aprire il file \"%s\"\n", getpid(), dest);
                              perror("openFile");
                             )
                        break;
                    }

                    // file aperto: scrivo in append il file e fornisco la cartella dove salvare eventuali file espulsi
                    if(appendToFile(dest, buf, file_sz, op->dirname) == -1) {
                        // errore di append
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile concatenare il file \"%s\"\n", getpid(), dest);
                              perror("appendToFile");
                        )
                        break;
                    }

                    PRINT(options->prints_on,
                        printf("Concatenato \"%s\" (%lu bytes) al file %s\n", src, file_sz, path);
                    )

                    if(closeFile(dest) == -1) {
                        // errore di chiusura: log su stderr
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile chiudere il file \"%s\"\n", getpid(), dest);
                              perror("closeFile");
                             )
                    }
                    break;
                }
                case 'W': {
                    // apro il file con la flag per crearlo
                    if(openFile(path, O_CREATEFILE) == -1) {
                        // errore di apertura: log su stderr
                        PRINT(options->prints_on,
                              fprintf(stderr, "[CLIENT %d]: Impossibile aprire (scrivere) il file \"%s\"\n", getpid(), path);
                              perror("openFile");
                             )
                        break;
                    }
                    // File aperto: scrivo il file e fornisco la cartella dove salvare eventuali file espulsi
                    // Se dirname fosse NULL i file restituiti vengono liberati internamente dalla API
                    if(writeFile(path, op->dirname) == -1) {
                        // errore di lettura
                        PRINT(options->prints_on,
                            fprintf(stderr, "[CLIENT %d]: Impossibile scrivere il file \"%s\"\n", getpid(), path);
                            perror("writeFile");
                        )
                    }
                    else {
                        PRINT(options->prints_on, printf("Scritto il file %s\n", path);)
                    }
                    if(closeFile(path) == -1) {
                        // errore di chiusura: log su stderr
                        PRINT(options->prints_on,
                            fprintf(stderr, "[CLIENT %d]: Impossibile chiudere il file \"%s\"\n", getpid(), path);
                            perror("closeFile");
                        )
                    }
                    break;
                }
                case 'w': {
                    // Scrittura di file da directories
                    // Visita ricorsivamente a partire da path e restituisce una lista di file
                    // il cui numero è <= options->max_write (o non limitato superiormente se tale campo è < 0)
                    struct Queue *lfiles = queue_init(); // inizializzo una coda vuota che conterrà i path dei file da scrivere
                    struct Queue *ldata = queue_init(); // inizializzo una coda vuota che conterrà i dati dei file da scrivere
                    long int nfiles = visit_dir(lfiles, ldata, path, op->max_write);
                    if(nfiles == -1) {
                        // errore nel leggere file da dir_write
                        fprintf(stderr, "Impossibile leggere files da %s: %s\n", path, strerror(errno));
                    }
                    else {
                        PRINT(options->prints_on, printf("Letti %ld files dalla directory %s\n", nfiles, path);)

                        struct node_t *file_data = NULL;
                        // scrivo i file nella coda nel fileserver
                        while((elem = pop(lfiles)) != NULL && (file_data = pop(ldata)) != NULL) {
                            path = (char*)elem->data;
                            int success = 0;
                            // apro il file con la flag per crearlo
                            if((success = openFile(path, O_CREATEFILE)) == -1) {
                                // errore di apertura: log su stderr
                                PRINT(options->prints_on,
                                      fprintf(stderr, "[CLIENT %d]: Impossibile aprire il file \"%s\"\n", getpid(), path);
                                )
                                break;
                            }
                            // scrivo i dati del file (la dimensione è il campo data_sz)
                            // La directory per lo swapout è contenuta nella struttura operazione
                            if((success = appendToFile(path, file_data->data, file_data->data_sz, op->dirname)) == -1) {
                                PRINT(options->prints_on,
                                    fprintf(stderr, "[CLIENT %d]: Impossibile concatenare il file \"%s\"\n", getpid(), path);
                                )
                            }
                            if(closeFile(path) == -1) {
                                // errore di chiusura: log su stderr
                                PRINT(options->prints_on,
                                    fprintf(stderr, "[CLIENT %d]: Impossibile chiudere il file \"%s\"\n", getpid(), path);
                                )
                            }

                            // libero la memoria occupata
                            free(elem->data);
                            free(elem);
                            free(file_data->data);
                            free(file_data);
                        }
                    }
                    free_Queue(lfiles);
                    free_Queue(ldata);
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

int save_reads(const char *dir, const int nbufs, const void **bufs, const size_t *sizes) {
    ;
}
